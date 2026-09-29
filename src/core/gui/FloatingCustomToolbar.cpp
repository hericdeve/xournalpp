/*
 * Xournal++
 *
 * Floating customizable toolbar widget
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "FloatingCustomToolbar.h"

#include <algorithm>

#include "control/Control.h"
#include "control/settings/Settings.h"
#include "gui/MainWindow.h"
#include "gui/ToolbarDefinitions.h"
#include "util/Util.h"
#include "util/gtk4_helper.h"

FloatingCustomToolbar::FloatingCustomToolbar(MainWindow* win, GtkOverlay* overlay):
        mainWindow(win), overlay(overlay, xoj::util::ref) {
    Settings* settings = mainWindow->getControl()->getSettings();
    bool horizontal = settings->isFloatingToolbarHorizontal();
    GtkOrientation orient = horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;

    // Root container box
    GtkWidget* rootBox = gtk_box_new(orient, 0);
    this->container.reset(rootBox, xoj::util::adopt);
    gtk_widget_set_name(rootBox, "floatingToolbarContainer");
    gtk_widget_add_css_class(rootBox, "osd");
    gtk_widget_add_css_class(rootBox, "floating-toolbar");

    // Inner GtkToolbar
    this->toolbar = gtk_toolbar_new();
    gtk_widget_set_name(this->toolbar, TOOLBAR_DEFINITIONS[TBFloatingIndex].guiName);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(this->toolbar), orient);
    gtk_toolbar_set_style(GTK_TOOLBAR(this->toolbar), GTK_TOOLBAR_ICONS);
    gtk_box_pack_start(GTK_BOX(rootBox), this->toolbar, TRUE, TRUE, 0);

    // Add root container into GtkOverlay
    gtk_overlay_add_overlay(overlay, rootBox);
    gtk_overlay_set_overlay_pass_through(overlay, rootBox, false);

    // Signals for positioning
    this->childPositionSignalId = g_signal_connect(
            overlay, "get-child-position", xoj::util::wrap_for_g_callback_v<getOverlayPosition>, this);

    // Event masks for non-button drag and cursor interaction
    gtk_widget_add_events(rootBox, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                   GDK_BUTTON1_MOTION_MASK | GDK_POINTER_MOTION_MASK |
                                   GDK_LEAVE_NOTIFY_MASK);
    gtk_widget_add_events(this->toolbar, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                         GDK_BUTTON1_MOTION_MASK | GDK_POINTER_MOTION_MASK |
                                         GDK_LEAVE_NOTIFY_MASK);

    g_signal_connect(rootBox, "button-press-event",
                     xoj::util::wrap_for_g_callback_v<onDragButtonPress>, this);
    g_signal_connect(rootBox, "motion-notify-event",
                     xoj::util::wrap_for_g_callback_v<onDragMotion>, this);
    g_signal_connect(rootBox, "button-release-event",
                     xoj::util::wrap_for_g_callback_v<onDragButtonRelease>, this);
    g_signal_connect(rootBox, "leave-notify-event",
                     xoj::util::wrap_for_g_callback_v<onDragLeave>, this);

    g_signal_connect(this->toolbar, "button-press-event",
                     xoj::util::wrap_for_g_callback_v<onDragButtonPress>, this);
    g_signal_connect(this->toolbar, "motion-notify-event",
                     xoj::util::wrap_for_g_callback_v<onDragMotion>, this);
    g_signal_connect(this->toolbar, "button-release-event",
                     xoj::util::wrap_for_g_callback_v<onDragButtonRelease>, this);
    g_signal_connect(this->toolbar, "leave-notify-event",
                     xoj::util::wrap_for_g_callback_v<onDragLeave>, this);

    // Keep child tool items hooked with drag event handlers as items are added/allocated
    g_signal_connect_swapped(this->toolbar, "size-allocate",
                             G_CALLBACK(+[](FloatingCustomToolbar* self) {
                                 self->updateToolItemDragHandlers();
                             }), this);

    gtk_widget_show_all(rootBox);
    setVisible(settings->isFloatingToolbarVisible());
}

FloatingCustomToolbar::~FloatingCustomToolbar() {
    if (this->childPositionSignalId > 0 && this->overlay) {
        g_signal_handler_disconnect(this->overlay.get(), this->childPositionSignalId);
        this->childPositionSignalId = 0;
    }
    if (this->overlay && this->container) {
        gtk_container_remove(GTK_CONTAINER(this->overlay.get()), this->container.get());
    }
}

auto FloatingCustomToolbar::getToolbarWidget() const -> GtkWidget* {
    return this->toolbar;
}

auto FloatingCustomToolbar::getContainerWidget() const -> GtkWidget* {
    return this->container.get();
}

bool FloatingCustomToolbar::isVisible() const {
    if (!this->container) {
        return false;
    }
    return gtk_widget_get_visible(this->container.get());
}

void FloatingCustomToolbar::setVisible(bool visible) {
    if (!this->container) {
        return;
    }

    if (this->inConfiguration) {
        gtk_widget_show(this->container.get());
        gtk_widget_show(this->toolbar);
        return;
    }

    if (visible) {
        gtk_widget_show(this->container.get());
        gtk_widget_show(this->toolbar);
        clampPosition();
        gtk_widget_queue_resize(this->container.get());
    } else {
        gtk_widget_hide(this->container.get());
    }
}

void FloatingCustomToolbar::setOrientation(GtkOrientation orientation) {
    bool horizontal = (orientation == GTK_ORIENTATION_HORIZONTAL);
    Settings* settings = mainWindow->getControl()->getSettings();
    settings->setFloatingToolbarHorizontal(horizontal);

    gtk_orientable_set_orientation(GTK_ORIENTABLE(this->container.get()), orientation);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(this->toolbar), orientation);

    if (!this->inConfiguration && mainWindow->getSelectedToolbar()) {
        mainWindow->reloadToolbars();
    }

    clampPosition();
    gtk_widget_queue_resize(this->container.get());
}

GtkOrientation FloatingCustomToolbar::getOrientation() const {
    return gtk_orientable_get_orientation(GTK_ORIENTABLE(this->toolbar));
}

void FloatingCustomToolbar::showForConfiguration() {
    this->inConfiguration = true;
    gtk_widget_show(this->container.get());
    gtk_widget_show(this->toolbar);
    clampPosition();
    gtk_widget_queue_resize(this->container.get());
}

void FloatingCustomToolbar::endConfiguration() {
    this->inConfiguration = false;
    Settings* settings = mainWindow->getControl()->getSettings();
    setVisible(settings->isFloatingToolbarVisible());
}

void FloatingCustomToolbar::connectItemDragHandlers(GtkWidget* item) {
    if (!item || g_object_get_data(G_OBJECT(item), "xopp_floating_drag_connected")) {
        return;
    }
    g_object_set_data(G_OBJECT(item), "xopp_floating_drag_connected", GINT_TO_POINTER(1));

    gtk_widget_add_events(item, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                 GDK_BUTTON1_MOTION_MASK | GDK_POINTER_MOTION_MASK |
                                 GDK_LEAVE_NOTIFY_MASK);

    g_signal_connect(item, "button-press-event",
                     xoj::util::wrap_for_g_callback_v<onDragButtonPress>, this);
    g_signal_connect(item, "motion-notify-event",
                     xoj::util::wrap_for_g_callback_v<onDragMotion>, this);
    g_signal_connect(item, "button-release-event",
                     xoj::util::wrap_for_g_callback_v<onDragButtonRelease>, this);
    g_signal_connect(item, "leave-notify-event",
                     xoj::util::wrap_for_g_callback_v<onDragLeave>, this);
}

void FloatingCustomToolbar::updateToolItemDragHandlers() {
    if (!this->toolbar || !GTK_IS_CONTAINER(this->toolbar)) {
        return;
    }
    gtk_container_foreach(GTK_CONTAINER(this->toolbar), [](GtkWidget* child, gpointer data) {
        static_cast<FloatingCustomToolbar*>(data)->connectItemDragHandlers(child);
    }, this);
}

void FloatingCustomToolbar::clampPosition() {
    if (!this->overlay || !this->container) {
        return;
    }

    if (!gtk_widget_get_mapped(GTK_WIDGET(this->overlay.get()))) {
        return;
    }

    int overlayWidth = gtk_widget_get_allocated_width(GTK_WIDGET(this->overlay.get()));
    int overlayHeight = gtk_widget_get_allocated_height(GTK_WIDGET(this->overlay.get()));

    GtkRequisition natSize;
    gtk_widget_get_preferred_size(this->container.get(), nullptr, &natSize);

    if (overlayWidth <= natSize.width || overlayHeight <= natSize.height) {
        return;
    }

    Settings* settings = mainWindow->getControl()->getSettings();
    int x = settings->getFloatingToolbarX();
    int y = settings->getFloatingToolbarY();

    int maxX = overlayWidth - natSize.width;
    int maxY = overlayHeight - natSize.height;

    x = std::clamp(x, 0, maxX);
    y = std::clamp(y, 0, maxY);
    settings->setFloatingToolbarX(x);
    settings->setFloatingToolbarY(y);
}

auto FloatingCustomToolbar::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* alloc,
                                              FloatingCustomToolbar* self) -> bool {
    if (widget != self->container.get()) {
        return false;
    }

    Settings* settings = self->mainWindow->getControl()->getSettings();
    int x = settings->getFloatingToolbarX();
    int y = settings->getFloatingToolbarY();

    int overlayWidth = gtk_widget_get_allocated_width(GTK_WIDGET(overlay));
    int overlayHeight = gtk_widget_get_allocated_height(GTK_WIDGET(overlay));

    GtkRequisition natSize;
    gtk_widget_get_preferred_size(widget, nullptr, &natSize);

    alloc->width = std::max(natSize.width, 36);
    alloc->height = std::max(natSize.height, 36);

    if (overlayWidth > alloc->width && overlayHeight > alloc->height) {
        int maxX = overlayWidth - alloc->width;
        int maxY = overlayHeight - alloc->height;
        x = std::clamp(x, 0, maxX);
        y = std::clamp(y, 0, maxY);
    }

    alloc->x = x;
    alloc->y = y;

    return true;
}

auto FloatingCustomToolbar::findLeafWidgetAt(GtkWidget* root, GtkWidget* widget, int rootX, int rootY) -> GtkWidget* {
    if (!widget || !gtk_widget_get_visible(widget) || !gtk_widget_get_mapped(widget)) {
        return nullptr;
    }

    int widgetX = 0;
    int widgetY = 0;
    if (!gtk_widget_translate_coordinates(root, widget, rootX, rootY, &widgetX, &widgetY)) {
        return nullptr;
    }

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    if (widgetX < 0 || widgetX >= alloc.width || widgetY < 0 || widgetY >= alloc.height) {
        return nullptr;
    }

    if (GTK_IS_CONTAINER(widget)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        GtkWidget* deepestChild = nullptr;
        for (GList* l = g_list_last(children); l != nullptr; l = l->prev) {
            GtkWidget* child = GTK_WIDGET(l->data);
            deepestChild = findLeafWidgetAt(root, child, rootX, rootY);
            if (deepestChild) {
                break;
            }
        }
        g_list_free(children);

        if (deepestChild) {
            return deepestChild;
        }
    }

    return widget;
}

auto FloatingCustomToolbar::isInteractiveControl(GtkWidget* leaf, GtkWidget* topContainer) -> bool {
    for (GtkWidget* curr = leaf; curr != nullptr && curr != topContainer; curr = gtk_widget_get_parent(curr)) {
        if (GTK_IS_BUTTON(curr) || GTK_IS_SCALE(curr) || GTK_IS_SPIN_BUTTON(curr) ||
            GTK_IS_COMBO_BOX(curr) || GTK_IS_ENTRY(curr) || GTK_IS_MENU_BUTTON(curr) ||
            GTK_IS_SWITCH(curr) || GTK_IS_COLOR_CHOOSER(curr)) {
            return true;
        }
    }
    return false;
}

auto FloatingCustomToolbar::onDragButtonPress(GtkWidget* widget, GdkEventButton* event,
                                              FloatingCustomToolbar* self) -> gboolean {
    if (event->button != GDK_BUTTON_PRIMARY) {
        return FALSE;
    }

    int rootX = 0;
    int rootY = 0;
    if (!gtk_widget_translate_coordinates(widget, self->container.get(),
                                          static_cast<int>(event->x), static_cast<int>(event->y),
                                          &rootX, &rootY)) {
        return FALSE;
    }

    GtkWidget* leaf = findLeafWidgetAt(self->container.get(), self->container.get(), rootX, rootY);
    if (isInteractiveControl(leaf, self->container.get())) {
        return FALSE;
    }

    self->isDragging = true;
    self->dragStartX = event->x_root;
    self->dragStartY = event->y_root;

    Settings* settings = self->mainWindow->getControl()->getSettings();
    self->initialPosX = settings->getFloatingToolbarX();
    self->initialPosY = settings->getFloatingToolbarY();

    GdkWindow* gdkWin = gtk_widget_get_window(self->container.get());
    if (gdkWin) {
        GdkDisplay* display = gdk_window_get_display(gdkWin);
        GdkCursor* cursor = gdk_cursor_new_from_name(display, "grabbing");
        gdk_window_set_cursor(gdkWin, cursor);
        if (cursor) {
            g_object_unref(cursor);
        }
    }
    return TRUE;
}

auto FloatingCustomToolbar::onDragMotion(GtkWidget* widget, GdkEventMotion* event,
                                         FloatingCustomToolbar* self) -> gboolean {
    if (self->isDragging) {
        double deltaX = event->x_root - self->dragStartX;
        double deltaY = event->y_root - self->dragStartY;

        int newX = self->initialPosX + static_cast<int>(deltaX);
        int newY = self->initialPosY + static_cast<int>(deltaY);

        Settings* settings = self->mainWindow->getControl()->getSettings();
        settings->setFloatingToolbarX(newX);
        settings->setFloatingToolbarY(newY);

        self->clampPosition();
        if (self->overlay) {
            gtk_widget_queue_allocate(GTK_WIDGET(self->overlay.get()));
        }
        return TRUE;
    } else {
        int rootX = 0;
        int rootY = 0;
        if (gtk_widget_translate_coordinates(widget, self->container.get(),
                                             static_cast<int>(event->x), static_cast<int>(event->y),
                                             &rootX, &rootY)) {
            GtkWidget* leaf = findLeafWidgetAt(self->container.get(), self->container.get(), rootX, rootY);
            GdkWindow* gdkWin = gtk_widget_get_window(self->container.get());
            if (gdkWin) {
                if (isInteractiveControl(leaf, self->container.get())) {
                    gdk_window_set_cursor(gdkWin, nullptr);
                } else {
                    GdkDisplay* display = gdk_window_get_display(gdkWin);
                    GdkCursor* cursor = gdk_cursor_new_from_name(display, "grab");
                    gdk_window_set_cursor(gdkWin, cursor);
                    if (cursor) {
                        g_object_unref(cursor);
                    }
                }
            }
        }
    }
    return FALSE;
}

auto FloatingCustomToolbar::onDragButtonRelease(GtkWidget* widget, GdkEventButton* event,
                                                FloatingCustomToolbar* self) -> gboolean {
    if (event->button == GDK_BUTTON_PRIMARY && self->isDragging) {
        self->isDragging = false;

        self->clampPosition();
        self->mainWindow->getControl()->getSettings()->save();

        GdkWindow* gdkWin = gtk_widget_get_window(self->container.get());
        if (gdkWin) {
            GdkDisplay* display = gdk_window_get_display(gdkWin);
            GdkCursor* cursor = gdk_cursor_new_from_name(display, "grab");
            gdk_window_set_cursor(gdkWin, cursor);
            if (cursor) {
                g_object_unref(cursor);
            }
        }
        if (self->overlay) {
            gtk_widget_queue_allocate(GTK_WIDGET(self->overlay.get()));
        }
        return TRUE;
    }
    return FALSE;
}

auto FloatingCustomToolbar::onDragLeave(GtkWidget* widget, GdkEventCrossing* event,
                                        FloatingCustomToolbar* self) -> gboolean {
    if (!self->isDragging) {
        GdkWindow* gdkWin = gtk_widget_get_window(self->container.get());
        if (gdkWin) {
            gdk_window_set_cursor(gdkWin, nullptr);
        }
    }
    return FALSE;
}
