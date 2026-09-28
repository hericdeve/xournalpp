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
    GtkWidget* rootBox = gtk_box_new(orient, 2);
    this->container.reset(rootBox, xoj::util::adopt);
    gtk_widget_set_name(rootBox, "floatingToolbarContainer");
    gtk_widget_add_css_class(rootBox, "osd");
    gtk_widget_add_css_class(rootBox, "floating-toolbar");

    // Handle box (contains drag grip, orientation toggle button, close button)
    // If toolbar is vertical, handle box is horizontal on top.
    // If toolbar is horizontal, handle box is vertical on left.
    GtkOrientation handleOrient = (orient == GTK_ORIENTATION_HORIZONTAL) ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
    this->handleBox = gtk_box_new(handleOrient, 2);
    gtk_widget_set_name(this->handleBox, "floatingToolbarHandle");
    gtk_box_pack_start(GTK_BOX(rootBox), this->handleBox, FALSE, FALSE, 0);

    // Drag handle event box
    GtkWidget* dragEventBox = gtk_event_box_new();
    gtk_widget_set_name(dragEventBox, "floatingToolbarDragHandle");
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(dragEventBox), FALSE);
    gtk_widget_add_events(dragEventBox, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                        GDK_BUTTON1_MOTION_MASK | GDK_POINTER_MOTION_MASK);

    this->dragGrip = gtk_image_new_from_icon_name("view-grid-symbolic", GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(dragEventBox), this->dragGrip);
    gtk_widget_set_tooltip_text(dragEventBox, "Drag to move floating toolbar");
    gtk_box_pack_start(GTK_BOX(this->handleBox), dragEventBox, TRUE, TRUE, 0);

    // Orientation toggle button
    this->toggleOrientationBtn = gtk_button_new_from_icon_name("object-rotate-right-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(this->toggleOrientationBtn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(this->toggleOrientationBtn, "Toggle horizontal / vertical orientation");
    gtk_box_pack_start(GTK_BOX(this->handleBox), this->toggleOrientationBtn, FALSE, FALSE, 0);

    // Close button
    this->closeBtn = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(this->closeBtn), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(this->closeBtn, "Hide floating toolbar");
    gtk_box_pack_start(GTK_BOX(this->handleBox), this->closeBtn, FALSE, FALSE, 0);

    // Inner GtkToolbar
    this->toolbar = gtk_toolbar_new();
    gtk_widget_set_name(this->toolbar, TOOLBAR_DEFINITIONS[TBFloatingIndex].guiName);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(this->toolbar), orient);
    gtk_toolbar_set_style(GTK_TOOLBAR(this->toolbar), GTK_TOOLBAR_ICONS);
    gtk_box_pack_start(GTK_BOX(rootBox), this->toolbar, TRUE, TRUE, 0);

    // Add root container into GtkOverlay
    gtk_overlay_add_overlay(overlay, rootBox);
    gtk_overlay_set_overlay_pass_through(overlay, rootBox, false);

    // Signals
    this->childPositionSignalId = g_signal_connect(
            overlay, "get-child-position", xoj::util::wrap_for_g_callback_v<getOverlayPosition>, this);

    g_signal_connect(dragEventBox, "button-press-event",
                     xoj::util::wrap_for_g_callback_v<onDragButtonPress>, this);
    g_signal_connect(dragEventBox, "motion-notify-event",
                     xoj::util::wrap_for_g_callback_v<onDragMotion>, this);
    g_signal_connect(dragEventBox, "button-release-event",
                     xoj::util::wrap_for_g_callback_v<onDragButtonRelease>, this);

    g_signal_connect(this->toggleOrientationBtn, "clicked",
                     xoj::util::wrap_for_g_callback_v<onOrientationToggleClicked>, this);
    g_signal_connect(this->closeBtn, "clicked",
                     xoj::util::wrap_for_g_callback_v<onCloseClicked>, this);

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
        // Show if there are items or visible
        gtk_widget_show(this->container.get());
        gtk_widget_show(this->toolbar);
        clampPosition();
        gtk_widget_queue_resize(this->container.get());
    } else {
        gtk_widget_hide(this->container.get());
    }
}

bool FloatingCustomToolbar::isVisible() const {
    return this->container && gtk_widget_is_visible(this->container.get());
}

void FloatingCustomToolbar::setOrientation(GtkOrientation orientation) {
    bool horizontal = (orientation == GTK_ORIENTATION_HORIZONTAL);
    Settings* settings = mainWindow->getControl()->getSettings();
    settings->setFloatingToolbarHorizontal(horizontal);

    gtk_orientable_set_orientation(GTK_ORIENTABLE(this->container.get()), orientation);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(this->toolbar), orientation);

    GtkOrientation handleOrient = horizontal ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
    gtk_orientable_set_orientation(GTK_ORIENTABLE(this->handleBox), handleOrient);

    if (!this->inConfiguration && mainWindow->getSelectedToolbar()) {
        mainWindow->reloadToolbars();
    }

    clampPosition();
    gtk_widget_queue_resize(this->container.get());
}

GtkOrientation FloatingCustomToolbar::getOrientation() const {
    return gtk_orientable_get_orientation(GTK_ORIENTABLE(this->toolbar));
}

void FloatingCustomToolbar::toggleOrientation() {
    GtkOrientation current = getOrientation();
    setOrientation(current == GTK_ORIENTATION_HORIZONTAL ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
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

void FloatingCustomToolbar::clampPosition() {
    if (!this->overlay || !this->container) {
        return;
    }

    Settings* settings = mainWindow->getControl()->getSettings();
    int x = settings->getFloatingToolbarX();
    int y = settings->getFloatingToolbarY();

    GtkAllocation overlayAlloc;
    gtk_widget_get_allocation(GTK_WIDGET(this->overlay.get()), &overlayAlloc);

    GtkRequisition natSize;
    gtk_widget_get_preferred_size(this->container.get(), nullptr, &natSize);

    int maxX = std::max(0, overlayAlloc.width - natSize.width);
    int maxY = std::max(0, overlayAlloc.height - natSize.height);

    if (overlayAlloc.width > 0 && overlayAlloc.height > 0) {
        x = std::clamp(x, 0, maxX);
        y = std::clamp(y, 0, maxY);
        settings->setFloatingToolbarX(x);
        settings->setFloatingToolbarY(y);
    }
}

auto FloatingCustomToolbar::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* alloc,
                                              FloatingCustomToolbar* self) -> bool {
    if (widget != self->container.get()) {
        return false;
    }

    Settings* settings = self->mainWindow->getControl()->getSettings();
    int x = settings->getFloatingToolbarX();
    int y = settings->getFloatingToolbarY();

    GtkAllocation overlayAlloc;
    gtk_widget_get_allocation(GTK_WIDGET(overlay), &overlayAlloc);

    GtkRequisition natSize;
    gtk_widget_get_preferred_size(widget, nullptr, &natSize);

    alloc->width = std::max(natSize.width, 36);
    alloc->height = std::max(natSize.height, 36);

    int maxX = std::max(0, overlayAlloc.width - alloc->width);
    int maxY = std::max(0, overlayAlloc.height - alloc->height);

    if (overlayAlloc.width > 0 && overlayAlloc.height > 0) {
        x = std::clamp(x, 0, maxX);
        y = std::clamp(y, 0, maxY);
    }

    alloc->x = x;
    alloc->y = y;

    return true;
}

auto FloatingCustomToolbar::onDragButtonPress(GtkWidget* widget, GdkEventButton* event,
                                              FloatingCustomToolbar* self) -> gboolean {
    if (event->button == GDK_BUTTON_PRIMARY) {
        self->isDragging = true;
        self->dragStartX = event->x_root;
        self->dragStartY = event->y_root;

        Settings* settings = self->mainWindow->getControl()->getSettings();
        self->initialPosX = settings->getFloatingToolbarX();
        self->initialPosY = settings->getFloatingToolbarY();

        GdkWindow* gdkWin = gtk_widget_get_window(widget);
        if (gdkWin) {
            GdkDisplay* display = gdk_window_get_display(gdkWin);
            GdkCursor* cursor = gdk_cursor_new_from_name(display, "grabbing");
            gdk_window_set_cursor(gdkWin, cursor);
            if (cursor) {
                g_object_unref(cursor);
            }
        }
        return TRUE;
    } else if (event->button == GDK_BUTTON_SECONDARY) {
        return onPopupMenu(widget, event, self);
    }
    return FALSE;
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
        gtk_widget_queue_resize(self->container.get());
        return TRUE;
    }
    return FALSE;
}

auto FloatingCustomToolbar::onDragButtonRelease(GtkWidget* widget, GdkEventButton* event,
                                                FloatingCustomToolbar* self) -> gboolean {
    if (event->button == GDK_BUTTON_PRIMARY && self->isDragging) {
        self->isDragging = false;

        GdkWindow* gdkWin = gtk_widget_get_window(widget);
        if (gdkWin) {
            gdk_window_set_cursor(gdkWin, nullptr);
        }
        return TRUE;
    }
    return FALSE;
}

void FloatingCustomToolbar::onOrientationToggleClicked(GtkButton* button, FloatingCustomToolbar* self) {
    self->toggleOrientation();
}

void FloatingCustomToolbar::onCloseClicked(GtkButton* button, FloatingCustomToolbar* self) {
    self->mainWindow->getControl()->setShowFloatingToolbar(false);
}

auto FloatingCustomToolbar::onPopupMenu(GtkWidget* widget, GdkEventButton* event,
                                        FloatingCustomToolbar* self) -> gboolean {
    if (event->button == GDK_BUTTON_SECONDARY) {
        GtkWidget* menu = gtk_menu_new();

        GtkWidget* itemToggleOrient = gtk_menu_item_new_with_label(
                self->getOrientation() == GTK_ORIENTATION_HORIZONTAL ? "Switch to Vertical" : "Switch to Horizontal");
        g_signal_connect_swapped(itemToggleOrient, "activate",
                                 G_CALLBACK(+[](FloatingCustomToolbar* c) { c->toggleOrientation(); }), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), itemToggleOrient);

        GtkWidget* itemClose = gtk_menu_item_new_with_label("Hide Floating Toolbar");
        g_signal_connect_swapped(itemClose, "activate",
                                 G_CALLBACK(+[](FloatingCustomToolbar* c) {
                                     c->mainWindow->getControl()->setShowFloatingToolbar(false);
                                 }), self);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), itemClose);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
        return TRUE;
    }
    return FALSE;
}
