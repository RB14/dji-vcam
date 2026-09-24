#include "preview_widget.h"

#include <QPainter>

PreviewWidget::PreviewWidget(QWidget* parent) : QOpenGLWidget(parent) { setMinimumSize(320, 180); }

void PreviewWidget::showFrame(const QImage& frame) {
    frame_ = frame;
    update();
}

void PreviewWidget::clear() {
    frame_ = QImage();
    update();
}

void PreviewWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (frame_.isNull()) {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, tr("No video"));
        return;
    }
    const QSize fitted = frame_.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, frame_);
}
