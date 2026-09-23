#include "PdfFloatingToolbox.h"

#include <algorithm>  // for max, min
#include <cmath>      // for abs
#include <cstddef>    // for size_t
#include <memory>
#include <string>   // for string
#include <utility>  // for move
#include <vector>   // for vector

#include <glib-object.h>  // for G_CALLBACK, g_signal_connect
#include <gtk/gtk.h>

#include "control/Control.h"      // for Control
#include "control/ToolEnums.h"    // for ToolType, TOOL_SELECT_PDF_TEXT_LI...
#include "control/ToolHandler.h"  // for ToolHandler
#include "control/tools/PdfElemSelection.h"
#include "gui/PageView.h"           // for XojPageView
#include "gui/XournalView.h"        // for XournalView
#include "model/Document.h"         // for Document
#include "model/Layer.h"            // for Layer
#include "model/PageRef.h"          // for PageRef
#include "model/Point.h"            // for Point
#include "model/Stroke.h"           // for Stroke, BUTT, StrokeTool::HIGHLIG...
#include "model/XojPage.h"          // for XojPage
#include "undo/GroupUndoAction.h"   // for GroupUndoAction
#include "undo/InsertUndoAction.h"  // for InsertUndoAction
#include "undo/UndoAction.h"        // for UndoAction
#include "undo/UndoRedoHandler.h"   // for UndoRedoHandler
#include "util/Assert.h"            // for xoj_assert
#include "util/gtk4_helper.h"       // for gtk_widget_get_clipboard

#include "MainWindow.h"  // for MainWindow

PdfFloatingToolbox::PdfFloatingToolbox(MainWindow* theMainWindow, GtkOverlay* overlay):
        theMainWindow(theMainWindow), overlay(overlay, xoj::util::ref), position({0, 0}) {
    this->floatingToolbox = theMainWindow->get("pdfFloatingToolbox");

    gtk_overlay_add_overlay(overlay, this->floatingToolbox);
    gtk_overlay_set_overlay_pass_through(overlay, this->floatingToolbox, true);

    this->getChildPositionId =
            g_signal_connect(overlay, "get-child-position", G_CALLBACK(this->getOverlayPosition), this);

    auto connectBtn = [theMainWindow, this](const char* name, GCallback cb, gulong& id) {
        if (GtkWidget* w = theMainWindow->get(name)) {
            id = g_signal_connect(w, "clicked", cb, this);
        }
    };

    connectBtn("pdfTbHighlight", G_CALLBACK(this->highlightCb), this->highlightId);
    connectBtn("pdfTbCopyText", G_CALLBACK(this->copyTextCb), this->copyTextId);
    connectBtn("pdfTbUnderline", G_CALLBACK(this->underlineCb), this->underlineId);
    connectBtn("pdfTbStrikethrough", G_CALLBACK(this->strikethroughCb), this->strikethroughId);
    connectBtn("pdfTbChangeType", G_CALLBACK(this->switchSelectTypeCb), this->switchSelectTypeId);
    connectBtn("pdfTbClose", G_CALLBACK(this->closeCb), this->closeId);

    this->clearSelection();
    this->hide();
}

PdfFloatingToolbox::~PdfFloatingToolbox() {
    if (this->getChildPositionId > 0 && this->overlay) {
        g_signal_handler_disconnect(this->overlay.get(), this->getChildPositionId);
        this->getChildPositionId = 0;
    }

    auto disconnectBtn = [this](const char* name, gulong& id) {
        if (id > 0 && this->theMainWindow) {
            if (GtkWidget* w = this->theMainWindow->get(name)) {
                g_signal_handler_disconnect(w, id);
            }
            id = 0;
        }
    };

    disconnectBtn("pdfTbHighlight", this->highlightId);
    disconnectBtn("pdfTbCopyText", this->copyTextId);
    disconnectBtn("pdfTbUnderline", this->underlineId);
    disconnectBtn("pdfTbStrikethrough", this->strikethroughId);
    disconnectBtn("pdfTbChangeType", this->switchSelectTypeId);
    disconnectBtn("pdfTbClose", this->closeId);
}

PdfElemSelection* PdfFloatingToolbox::getSelection() const { return this->pdfElemSelection.get(); }
bool PdfFloatingToolbox::hasSelection() const { return this->getSelection() != nullptr; }

void PdfFloatingToolbox::clearSelection() { this->pdfElemSelection.reset(); }

auto PdfFloatingToolbox::newSelection(double x, double y) -> const PdfElemSelection* {
    this->pdfElemSelection = std::make_unique<PdfElemSelection>(x, y, this->theMainWindow->getControl());
    return this->pdfElemSelection.get();
}

void PdfFloatingToolbox::show(int x, int y) {
    xoj_assert(this->getSelection());
    this->position = {x, y};
    this->show();

    // Record the color now: the active tool may change while the toolbox is up (e.g. if the tool is linked to a button)
    this->color = theMainWindow->getXournal()->getControl()->getToolHandler()->getColor();
}

void PdfFloatingToolbox::hide() {
    if (isHidden())
        return;

    gtk_widget_hide(this->floatingToolbox);
}

auto PdfFloatingToolbox::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                            PdfFloatingToolbox* self) -> gboolean {
    if (widget == self->floatingToolbox) {
        // Get existing width and height
        GtkRequisition natural;
        gtk_widget_get_preferred_size(widget, nullptr, &natural);
        allocation->width = natural.width;
        allocation->height = natural.height;

        // Position in GtkOverlay coordinate space
        const int gap = 5;
        int overlayWidth = gtk_widget_get_allocated_width(GTK_WIDGET(overlay));
        int overlayHeight = gtk_widget_get_allocated_height(GTK_WIDGET(overlay));

        bool rightOK = (self->position.x + gap + allocation->width <= overlayWidth - gap);
        bool bottomOK = (self->position.y + gap + allocation->height <= overlayHeight - gap);

        allocation->x = rightOK ? (self->position.x + gap) : (self->position.x - gap - allocation->width);
        allocation->y = bottomOK ? (self->position.y + gap) : (self->position.y - gap - allocation->height);

        int minX = gap;
        int maxX = std::max(minX, overlayWidth - allocation->width - gap);
        int minY = gap;
        int maxY = std::max(minY, overlayHeight - allocation->height - gap);

        allocation->x = std::clamp(allocation->x, minX, maxX);
        allocation->y = std::clamp(allocation->y, minY, maxY);

        return true;
    }

    return false;
}

void PdfFloatingToolbox::userCancelSelection() {
    this->pdfElemSelection.reset();
    this->hide();
}

void PdfFloatingToolbox::closeCb(GtkButton* button, PdfFloatingToolbox* pft) {
    pft->userCancelSelection();
    if (auto* xournal = pft->theMainWindow->getXournal()) {
        size_t p = xournal->getCurrentPage();
        if (auto* pv = xournal->getViewFor(p)) {
            pv->repaintPage();
        }
    }
}

void PdfFloatingToolbox::highlightCb(GtkButton* button, PdfFloatingToolbox* pft) {
    int markerOpacity = pft->theMainWindow->getControl()->getToolHandler()->getSelectPDFTextMarkerOpacity();
    pft->createStrokes(PdfMarkerStyle::POS_TEXT_MIDDLE, PdfMarkerStyle::WIDTH_TEXT_HEIGHT, markerOpacity);
    pft->userCancelSelection();
}

void PdfFloatingToolbox::copyTextCb(GtkButton* button, PdfFloatingToolbox* pft) {
    pft->copyTextToClipboard();
    pft->userCancelSelection();
}

void PdfFloatingToolbox::underlineCb(GtkButton* button, PdfFloatingToolbox* pft) {
    pft->createStrokes(PdfMarkerStyle::POS_TEXT_BOTTOM, PdfMarkerStyle::WIDTH_TEXT_LINE, 230);
    pft->userCancelSelection();
}

void PdfFloatingToolbox::strikethroughCb(GtkButton* button, PdfFloatingToolbox* pft) {
    pft->createStrokes(PdfMarkerStyle::POS_TEXT_MIDDLE, PdfMarkerStyle::WIDTH_TEXT_LINE, 230);
    pft->userCancelSelection();
}

void PdfFloatingToolbox::show() {
    gtk_widget_hide(this->floatingToolbox);  // force showing in new position
    gtk_widget_show_all(this->floatingToolbox);
}

void PdfFloatingToolbox::copyTextToClipboard() {
    GtkClipboard* clipboard = gtk_widget_get_clipboard(this->theMainWindow->getWindow());
    if (const std::string& text = this->pdfElemSelection->getSelectedText(); !text.empty()) {
        gtk_clipboard_set_text(clipboard, text.c_str(), -1);
    }
}

void PdfFloatingToolbox::createStrokes(PdfMarkerStyle position, PdfMarkerStyle width, int markerOpacity) {
    if (!this->pdfElemSelection) {
        return;
    }

    const size_t pdfPageNo = this->pdfElemSelection->getSelectionPageNr();
    auto doc = this->theMainWindow->getControl()->getDocument();

    doc->lock_shared();
    const size_t targetPageNo = doc->findPdfPage(pdfPageNo);
    PageRef page = (targetPageNo != npos) ? doc->getPage(targetPageNo) : nullptr;
    doc->unlock_shared();

    if (!page) {
        g_warning("Could not find document page corresponding to PDF page %zu!", pdfPageNo);
        return;
    }

    const auto textRects = this->pdfElemSelection->getSelectedTextRects();
    if (textRects.empty()) {
        return;
    }

    auto* control = this->theMainWindow->getControl();
    Layer* layer = page->getSelectedLayer();

    Range dirtyRange;
    std::vector<ElementPtr> strokes;
    for (XojPdfRectangle rect: textRects) {
        const double topOfLine = std::min(rect.y1, rect.y2);
        const double middleOfLine = (rect.y1 + rect.y2) / 2;
        const double bottomOfLine = std::max(rect.y1, rect.y2);
        const double rectWidth = std::abs(rect.y2 - rect.y1);

        // the center line position of stroke
        const double h = position == PdfMarkerStyle::POS_TEXT_BOTTOM ? bottomOfLine :
                         position == PdfMarkerStyle::POS_TEXT_MIDDLE ? middleOfLine :
                                                                       topOfLine;
        // the width of stroke
        const double w = width == PdfMarkerStyle::WIDTH_TEXT_LINE ? 1 : rectWidth;

        auto stroke = std::make_unique<Stroke>();
        stroke->setColor(this->color);
        stroke->setFill(markerOpacity);
        stroke->setToolType(StrokeTool::HIGHLIGHTER);
        stroke->setWidth(w);
        stroke->addPoint(Point(rect.x1, h, -1));
        stroke->addPoint(Point(rect.x2, h, -1));
        stroke->setStrokeCapStyle(StrokeCapStyle::BUTT);

        dirtyRange.addPoint(rect.x1, h - 0.5 * w);
        dirtyRange.addPoint(rect.x2, h + 0.5 * w);

        strokes.push_back(std::move(stroke));
    }

    std::vector<const Element*> strokePtrs(strokes.size());
    std::transform(strokes.begin(), strokes.end(), strokePtrs.begin(), [](auto& e) { return e.get(); });

    doc->lock();
    for (auto&& s: strokes) {
        layer->addElement(std::move(s));
    }
    doc->unlock();
    page->fireElementsChanged(strokePtrs, dirtyRange);

    auto undoAct = std::make_unique<GroupUndoAction>();
    for (auto* stroke: strokePtrs) {
        undoAct->addAction(std::make_unique<InsertUndoAction>(page, layer, stroke));
    }
    control->getUndoRedoHandler()->addUndoAction(std::move(undoAct));
}

void PdfFloatingToolbox::switchSelectTypeCb(GtkButton* button, PdfFloatingToolbox* pft) {
    if (!pft->pdfElemSelection) {
        return;
    }

    ToolType type = pft->theMainWindow->getControl()->getToolHandler()->getToolType();

    type = type == ToolType::TOOL_SELECT_PDF_TEXT_LINEAR ? ToolType::TOOL_SELECT_PDF_TEXT_RECT :
                                                           ToolType::TOOL_SELECT_PDF_TEXT_LINEAR;

    pft->theMainWindow->getControl()->selectTool(type);

    pft->pdfElemSelection->setToolType(type);
    pft->selectionStyle = PdfElemSelection::selectionStyleForToolType(type);
    pft->pdfElemSelection->finalizeSelectionAndRepaint(pft->selectionStyle);
}

bool PdfFloatingToolbox::isHidden() const { return !gtk_widget_is_visible(this->floatingToolbox); }
