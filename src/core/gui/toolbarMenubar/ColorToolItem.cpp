#include "ColorToolItem.h"

#include <algorithm>  // for std::remove
#include <utility>    // for move

#include "enums/Action.enum.h"                  // for Action
#include "gui/toolbarMenubar/icon/ColorIcon.h"  // for ColorIcon
#include "util/GtkUtil.h"                       // for setToggleButtonUnreleasable
#include "util/gtk4_helper.h"                   // for gtk_button_set_child

ColorToolItem::ColorToolItem(NamedColor namedColor, const std::optional<Recolor>& recolor):
        AbstractToolItem(std::string("COLOR(") + std::to_string(namedColor.getIndex()) + ")", Category::COLORS),
        namedColor(std::move(namedColor)),
        target(xoj::util::makeGVariantSPtr(this->namedColor.getColor())),
        recolor(recolor) {}

ColorToolItem::~ColorToolItem() {
    for (auto* btn: this->buttons) {
        if (GTK_IS_WIDGET(btn)) {
            g_signal_handlers_disconnect_by_data(btn, this);
        }
    }
    this->buttons.clear();

    for (auto* icon: this->proxyIcons) {
        if (GTK_IS_WIDGET(icon)) {
            g_signal_handlers_disconnect_by_data(icon, this);
        }
    }
    this->proxyIcons.clear();
}

auto ColorToolItem::getColor() const -> Color { return this->namedColor.getColor(); }

auto ColorToolItem::getDisplayedColor() const -> Color {
    if (this->recolor) {
        return this->recolor->convertColor(this->namedColor.getColor());
    }
    return this->namedColor.getColor();
}

auto ColorToolItem::createItem(bool) -> xoj::util::WidgetSPtr {
    auto* btn = gtk_toggle_button_new();
    gtk_widget_set_can_focus(btn, false);  // todo(gtk4) not necessary anymore
    auto actionName = std::string("win.") + Action_toString(Action::TOOL_COLOR);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), actionName.data());
    gtk_actionable_set_action_target_value(GTK_ACTIONABLE(btn), target.get());
    xoj::util::gtk::setToggleButtonUnreleasable(GTK_TOGGLE_BUTTON(btn));

    gtk_widget_set_tooltip_text(btn, this->namedColor.getName().c_str());
    gtk_button_set_child(GTK_BUTTON(btn), getNewToolIcon());

    this->buttons.push_back(btn);
    g_signal_connect(btn, "destroy", G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) {
                         auto* self = static_cast<ColorToolItem*>(user_data);
                         auto& b = self->buttons;
                         b.erase(std::remove(b.begin(), b.end(), widget), b.end());
                     }),
                     this);

    // For the sake of deprecated GtkToolbar, wrap the button in a GtkToolItem
    // Todo(gtk4): remove
    GtkToolItem* it = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(it), btn);
    /// Makes a proxy item for the toolbar's overflow menu
    auto createProxy = [this]() {
        GtkWidget* proxy = gtk_check_menu_item_new();
        gtk_check_menu_item_set_draw_as_radio(GTK_CHECK_MENU_ITEM(proxy), true);

        auto* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_container_add(GTK_CONTAINER(proxy), box);
        GtkWidget* icon = getNewToolIcon();
        gtk_box_append(GTK_BOX(box), icon);
        this->proxyIcons.push_back(icon);
        g_signal_connect(icon, "destroy", G_CALLBACK(+[](GtkWidget* widget, gpointer user_data) {
                             auto* self = static_cast<ColorToolItem*>(user_data);
                             auto& icons = self->proxyIcons;
                             icons.erase(std::remove(icons.begin(), icons.end(), widget), icons.end());
                         }),
                         this);
        gtk_box_append(GTK_BOX(box), gtk_label_new(getToolDisplayName().c_str()));

        gtk_actionable_set_action_name(GTK_ACTIONABLE(proxy),
                                       (std::string("win.") + Action_toString(Action::TOOL_COLOR)).c_str());
        if (target) {
            gtk_actionable_set_action_target_value(GTK_ACTIONABLE(proxy), target.get());
        }
        xoj::util::gtk::fixActionableInitialSensitivity(GTK_ACTIONABLE(proxy));
        return proxy;
    };
    gtk_tool_item_set_proxy_menu_item(it, "", createProxy());
    return xoj::util::WidgetSPtr(GTK_WIDGET(it), xoj::util::adopt);
}

auto ColorToolItem::getToolDisplayName() const -> std::string { return this->namedColor.getName(); }

auto ColorToolItem::getNewToolIcon() const -> GtkWidget* {
    return ColorIcon::newGtkImage(getDisplayedColor(), 16, true);
}

void ColorToolItem::updateColor(const Palette& palette) {
    this->namedColor = palette.getColorAt(this->namedColor.getIndex());
    this->target = xoj::util::makeGVariantSPtr(this->namedColor.getColor());
    for (auto* btn: this->buttons) {
        if (GTK_IS_WIDGET(btn)) {
            gtk_widget_set_tooltip_text(btn, this->namedColor.getName().c_str());
            gtk_actionable_set_action_target_value(GTK_ACTIONABLE(btn), this->target.get());
        }
    }
    updateRecolor(this->recolor);
}

void ColorToolItem::updateRecolor(const std::optional<Recolor>& recolor) {
    this->recolor = recolor;
    for (auto* btn: this->buttons) {
        if (GTK_IS_WIDGET(btn)) {
            gtk_button_set_child(GTK_BUTTON(btn), getNewToolIcon());
        }
    }
    for (auto* icon: this->proxyIcons) {
        if (GTK_IS_WIDGET(icon) && GTK_IS_IMAGE(icon)) {
            auto pixbuf = ColorIcon::newGdkPixbuf(getDisplayedColor(), 16, true);
            gtk_image_set_from_pixbuf(GTK_IMAGE(icon), pixbuf.get());
        }
    }
}

void ColorToolItem::updateSecondaryColor(const std::optional<Recolor>& recolor) {
    updateRecolor(recolor);
}
