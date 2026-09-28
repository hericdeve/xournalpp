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

#pragma once

#include <gtk/gtk.h>
#include <string>

#include "util/raii/GObjectSPtr.h"

class MainWindow;

class FloatingCustomToolbar {
public:
    FloatingCustomToolbar(MainWindow* win, GtkOverlay* overlay);
    virtual ~FloatingCustomToolbar();

    FloatingCustomToolbar(const FloatingCustomToolbar&) = delete;
    FloatingCustomToolbar& operator=(const FloatingCustomToolbar&) = delete;
    FloatingCustomToolbar(FloatingCustomToolbar&&) = delete;
    FloatingCustomToolbar& operator=(FloatingCustomToolbar&&) = delete;

    void setVisible(bool visible);
    bool isVisible() const;

    void setOrientation(GtkOrientation orientation);
    GtkOrientation getOrientation() const;

    void showForConfiguration();
    void endConfiguration();

    void updateToolItemDragHandlers();

    GtkWidget* getToolbarWidget() const;
    GtkWidget* getContainerWidget() const;

private:
    static bool getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* alloc,
                                   FloatingCustomToolbar* self);
    static gboolean onDragButtonPress(GtkWidget* widget, GdkEventButton* event, FloatingCustomToolbar* self);
    static gboolean onDragMotion(GtkWidget* widget, GdkEventMotion* event, FloatingCustomToolbar* self);
    static gboolean onDragButtonRelease(GtkWidget* widget, GdkEventButton* event, FloatingCustomToolbar* self);
    static gboolean onDragLeave(GtkWidget* widget, GdkEventCrossing* event, FloatingCustomToolbar* self);

    static auto findLeafWidgetAt(GtkWidget* root, GtkWidget* widget, int rootX, int rootY) -> GtkWidget*;
    static auto isInteractiveControl(GtkWidget* leaf, GtkWidget* topContainer) -> bool;

    void connectItemDragHandlers(GtkWidget* item);
    void clampPosition();

private:
    MainWindow* mainWindow{nullptr};
    xoj::util::GObjectSPtr<GtkOverlay> overlay;

    xoj::util::WidgetSPtr container;
    GtkWidget* toolbar{nullptr};

    bool isDragging{false};
    double dragStartX{0};
    double dragStartY{0};
    int initialPosX{0};
    int initialPosY{0};

    gulong childPositionSignalId{0};
    bool inConfiguration{false};
};
