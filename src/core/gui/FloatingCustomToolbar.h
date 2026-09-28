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
    void toggleOrientation();

    void showForConfiguration();
    void endConfiguration();

    GtkWidget* getToolbarWidget() const;
    GtkWidget* getContainerWidget() const;

private:
    static bool getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* alloc,
                                   FloatingCustomToolbar* self);
    static gboolean onDragButtonPress(GtkWidget* widget, GdkEventButton* event, FloatingCustomToolbar* self);
    static gboolean onDragMotion(GtkWidget* widget, GdkEventMotion* event, FloatingCustomToolbar* self);
    static gboolean onDragButtonRelease(GtkWidget* widget, GdkEventButton* event, FloatingCustomToolbar* self);
    static void onOrientationToggleClicked(GtkButton* button, FloatingCustomToolbar* self);
    static void onCloseClicked(GtkButton* button, FloatingCustomToolbar* self);
    static gboolean onPopupMenu(GtkWidget* widget, GdkEventButton* event, FloatingCustomToolbar* self);

    void clampPosition();
    void updateOrientationUI();

private:
    MainWindow* mainWindow{nullptr};
    xoj::util::GObjectSPtr<GtkOverlay> overlay;

    xoj::util::WidgetSPtr container;
    GtkWidget* handleBox{nullptr};
    GtkWidget* dragGrip{nullptr};
    GtkWidget* toggleOrientationBtn{nullptr};
    GtkWidget* closeBtn{nullptr};
    GtkWidget* toolbar{nullptr};

    bool isDragging{false};
    double dragStartX{0};
    double dragStartY{0};
    int initialPosX{0};
    int initialPosY{0};

    gulong childPositionSignalId{0};
    bool inConfiguration{false};
};
