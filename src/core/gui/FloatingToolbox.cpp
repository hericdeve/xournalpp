#include "FloatingToolbox.h"

#include <algorithm>  // for max
#include <memory>     // for allocator

#include <gdk/gdk.h>      // for GdkRectangle, GDK_LEAVE_...
#include <glib-object.h>  // for G_CALLBACK, g_signal_con...

#include "control/Control.h"                 // for Control
#include "control/ToolEnums.h"               // for TOOL_FLOATING_TOOLBOX
#include "control/settings/ButtonConfig.h"   // for ButtonConfig
#include "control/settings/Settings.h"       // for Settings
#include "control/settings/SettingsEnums.h"  // for BUTTON_COUNT
#include "util/glib_casts.h"

#include "MainWindow.h"          // for MainWindow
#include "ToolbarDefinitions.h"  // for ToolbarEntryDefintion
#include "XournalView.h"


FloatingToolbox::FloatingToolbox(MainWindow* theMainWindow, GtkOverlay* overlay):
        mainWindow(theMainWindow),
        floatingToolbox(theMainWindow->get("floatingToolbox")),
        overlay(overlay, xoj::util::ref),
        floatingToolboxX(200),
        floatingToolboxY(200),
        floatingToolboxState(recalcSize) {

    gtk_overlay_add_overlay(overlay, this->floatingToolbox);
    gtk_overlay_set_overlay_pass_through(overlay, this->floatingToolbox, true);
    gtk_widget_add_events(this->floatingToolbox, GDK_LEAVE_NOTIFY_MASK | GDK_ENTER_NOTIFY_MASK);

    this->leaveNotifyId = g_signal_connect(this->floatingToolbox, "leave-notify-event",
                                           xoj::util::wrap_for_g_callback_v<handleLeaveFloatingToolbox>, this);
    this->enterNotifyId = g_signal_connect(this->floatingToolbox, "enter-notify-event",
                                           xoj::util::wrap_for_g_callback_v<handleEnterFloatingToolbox>, this);
    // position overlay widgets
    this->getChildPositionId =
            g_signal_connect(overlay, "get-child-position", xoj::util::wrap_for_g_callback_v<getOverlayPosition>, this);
}


FloatingToolbox::~FloatingToolbox() {
    this->cancelScheduledHide();
    if (this->enterNotifyId > 0 && this->floatingToolbox) {
        g_signal_handler_disconnect(this->floatingToolbox, this->enterNotifyId);
        this->enterNotifyId = 0;
    }
    if (this->leaveNotifyId > 0 && this->floatingToolbox) {
        g_signal_handler_disconnect(this->floatingToolbox, this->leaveNotifyId);
        this->leaveNotifyId = 0;
    }
    if (this->getChildPositionId > 0 && this->overlay) {
        g_signal_handler_disconnect(this->overlay.get(), this->getChildPositionId);
        this->getChildPositionId = 0;
    }
}


void FloatingToolbox::show(int x, int y) {
    this->cancelScheduledHide();
    this->floatingToolboxX = x;
    this->floatingToolboxY = y;
    this->floatingToolboxState = recalcSize;
    this->show();
    gtk_widget_queue_resize(this->floatingToolbox);
}


/****
 * floatingToolboxActivated
 *  True if the user has:
 *    assigned a mouse or stylus button to bring up the floatingToolbox;
 *    or enabled tapAction and Show FloatingToolbox( prefs->DrawingArea->ActionOnToolTap );
 *    or put tools in the FloatingToolbox.
 *
 */
auto FloatingToolbox::floatingToolboxActivated() -> bool {
    Settings* settings = this->mainWindow->getControl()->getSettings();
    ButtonConfig* cfg = nullptr;

    // check if any buttons assigned to bring up toolbox
    for (unsigned int id = 0; id < BUTTON_COUNT; id++) {
        cfg = settings->getButtonConfig(id);

        if (cfg->getAction() == TOOL_FLOATING_TOOLBOX) {
            return true;  // return true
        }
    }

    // check if user can show Floating Menu with tap.
    if (settings->getDoActionOnStrokeFiltered() && settings->getStrokeFilterEnabled()) {
        return true;  // return true
    }

    return this->hasWidgets();
}


auto FloatingToolbox::hasWidgets() -> bool {
    for (int index = TBFloatFirst; index <= TBFloatLast; index++) {
        GtkToolbar* toolbar1 = GTK_TOOLBAR(this->mainWindow->get(TOOLBAR_DEFINITIONS[index].guiName));
        if (gtk_toolbar_get_n_items(toolbar1) > 0) {
            return true;
        }
    }
    return false;
}


void FloatingToolbox::showForConfiguration() {
    this->cancelScheduledHide();
    this->floatingToolboxState = configuration;
    this->show();
    gtk_widget_queue_resize(this->floatingToolbox);
}


void FloatingToolbox::show() {
    gtk_widget_show(this->floatingToolbox);

    bool isConfig = (this->floatingToolboxState == configuration);
    bool anyWidgets = this->hasWidgets();

    gtk_widget_set_visible(this->mainWindow->get("labelFloatingToolbox"), isConfig);

    for (int index = TBFloatFirst; index <= TBFloatLast; index++) {
        GtkWidget* tbWidget = this->mainWindow->get(TOOLBAR_DEFINITIONS[index].guiName);
        if (tbWidget) {
            if (isConfig) {
                gtk_widget_show_all(tbWidget);
            } else {
                GtkToolbar* toolbar = GTK_TOOLBAR(tbWidget);
                bool hasItems = (gtk_toolbar_get_n_items(toolbar) > 0);
                if (hasItems) {
                    gtk_widget_show_all(tbWidget);
                } else {
                    gtk_widget_hide(tbWidget);
                }
            }
        }
    }

    gtk_widget_set_visible(this->mainWindow->get("showIfEmpty"), !isConfig && !anyWidgets);
    GtkWidget* vbox = this->mainWindow->get("floatingvbox");
    if (vbox) {
        gtk_widget_show(vbox);
    }
}


void FloatingToolbox::hide() {
    this->cancelScheduledHide();
    this->floatingToolboxState = recalcSize;
    gtk_widget_hide(this->floatingToolbox);
}


bool FloatingToolbox::isVisible() const {
    return this->floatingToolbox && gtk_widget_is_visible(this->floatingToolbox);
}


bool FloatingToolbox::isConfiguring() const {
    return this->floatingToolboxState == configuration;
}


void FloatingToolbox::flagRecalculateSizeRequired() {
    this->floatingToolboxState = recalcSize;
    if (this->floatingToolbox) {
        gtk_widget_queue_resize(this->floatingToolbox);
    }
}


/**
 * getOverlayPosition - this is how we position the widget in the overlay under the mouse
 *
 * The requested location is communicated via the FloatingToolbox member variables:
 * ->floatingToolbox,		so we can operate on the right widget
 * ->floatingToolboxState,	are we configuring, resizing or just moving
 * ->floatingToolboxX,		where to display (in GtkOverlay coordinates)
 * ->floatingToolboxY.
 *
 */
auto FloatingToolbox::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                         FloatingToolbox* self) -> bool {
    if (widget != self->floatingToolbox) {
        return false;
    }

    gtk_widget_get_allocation(widget, allocation);

    if (self->floatingToolboxState != noChange || allocation->height < 2) {
        GtkRequisition natural;
        gtk_widget_get_preferred_size(widget, nullptr, &natural);
        allocation->width = natural.width;
        allocation->height = natural.height;
    }

    switch (self->floatingToolboxState) {
        case recalcSize:
            [[fallthrough]];
        case noChange: {
            int centerX = self->floatingToolboxX - allocation->width / 2;
            int centerY = self->floatingToolboxY - allocation->height / 2;

            constexpr int margin = 10;
            int overlayWidth = gtk_widget_get_allocated_width(GTK_WIDGET(overlay));
            int overlayHeight = gtk_widget_get_allocated_height(GTK_WIDGET(overlay));

            int minX = margin;
            int maxX = std::max(minX, overlayWidth - allocation->width - margin);
            int minY = margin;
            int maxY = std::max(minY, overlayHeight - allocation->height - margin);

            allocation->x = std::clamp(centerX, minX, maxX);
            allocation->y = std::clamp(centerY, minY, maxY);
            self->floatingToolboxState = noChange;
            break;
        }

        case configuration:
            allocation->x = 40;
            allocation->y = 40;
            allocation->width = std::max(allocation->width + 32, 50);
            allocation->height = std::max(allocation->height, 50);
            break;
    }

    return true;
}


static bool isPopoverActive(GtkWidget* widget) {
    if (!widget) {
        return false;
    }
    if (GTK_IS_MENU_BUTTON(widget)) {
        GtkPopover* popover = gtk_menu_button_get_popover(GTK_MENU_BUTTON(widget));
        if (popover && gtk_widget_is_visible(GTK_WIDGET(popover))) {
            return true;
        }
    }
    if (GTK_IS_CONTAINER(widget)) {
        bool active = false;
        gtk_container_forall(
                GTK_CONTAINER(widget),
                +[](GtkWidget* child, gpointer data) {
                    auto* pActive = static_cast<bool*>(data);
                    if (!*pActive && isPopoverActive(child)) {
                        *pActive = true;
                    }
                },
                &active);
        return active;
    }
    return false;
}


void FloatingToolbox::scheduleHide(guint delayMs) {
    if (this->floatingToolboxState == configuration) {
        return;
    }
    this->cancelScheduledHide();
    this->scheduledHideTimer = g_timeout_add(delayMs, xoj::util::wrap_for_once_v<onScheduledHideTimeout>, this);
}


void FloatingToolbox::cancelScheduledHide() {
    this->scheduledHideTimer.cancel();
}


void FloatingToolbox::onScheduledHideTimeout(FloatingToolbox* self) {
    self->scheduledHideTimer.consume();
    if (!self->isVisible() || self->floatingToolboxState == configuration) {
        return;
    }
    if (isPopoverActive(self->floatingToolbox)) {
        return;
    }
    if (!self->isPointerInside()) {
        self->hide();
    }
}


bool FloatingToolbox::isPointerInside() const {
    if (!this->floatingToolbox || !gtk_widget_get_mapped(this->floatingToolbox) ||
        !gtk_widget_is_visible(this->floatingToolbox)) {
        return false;
    }

    GtkWidget* toplevel = gtk_widget_get_toplevel(this->floatingToolbox);
    if (!toplevel || !gtk_widget_is_toplevel(toplevel)) {
        return false;
    }

    gint tx = 0;
    gint ty = 0;
    if (!gtk_widget_translate_coordinates(this->floatingToolbox, toplevel, 0, 0, &tx, &ty)) {
        return false;
    }

    GdkWindow* toplevelWindow = gtk_widget_get_window(toplevel);
    if (!toplevelWindow) {
        return false;
    }

    GdkDisplay* display = gtk_widget_get_display(this->floatingToolbox);
    if (!display) {
        return false;
    }

    GdkSeat* seat = gdk_display_get_default_seat(display);
    if (!seat) {
        return false;
    }

    GdkDevice* device = gdk_seat_get_pointer(seat);
    if (!device) {
        return false;
    }

    gint px = 0;
    gint py = 0;
    gdk_window_get_device_position(toplevelWindow, device, &px, &py, nullptr);

    GtkAllocation alloc;
    gtk_widget_get_allocation(this->floatingToolbox, &alloc);

    return (px >= tx && px < tx + alloc.width && py >= ty && py < ty + alloc.height);
}


bool FloatingToolbox::handleEnterFloatingToolbox(GtkWidget* floatingToolbox, GdkEvent* event, FloatingToolbox* self) {
    if (floatingToolbox == self->floatingToolbox) {
        if (event->type == GDK_ENTER_NOTIFY) {
            self->cancelScheduledHide();
        }
        return true;
    }
    return false;
}


bool FloatingToolbox::handleLeaveFloatingToolbox(GtkWidget* floatingToolbox, GdkEvent* event, FloatingToolbox* self) {
    if (floatingToolbox == self->floatingToolbox) {
        if (event->type == GDK_LEAVE_NOTIFY) {
            if (event->crossing.detail == GDK_NOTIFY_INFERIOR) {
                self->cancelScheduledHide();
                return true;
            }

            // Do not dismiss while a child popover/menu is open
            if (isPopoverActive(self->floatingToolbox)) {
                return true;
            }

            if (self->floatingToolboxState != configuration) {
                self->scheduleHide(150);
            }
        }
        return true;
    }
    return false;
}
