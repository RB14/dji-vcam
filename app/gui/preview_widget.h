// Shows the latest live-view frame, letterboxed to keep its aspect ratio. Drawn through OpenGL,
// so scaling the frame to the window (and high-DPI screens) happens on the GPU.
#pragma once

#include <QImage>
#include <QOpenGLWidget>

class PreviewWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit PreviewWidget(QWidget* parent = nullptr);

public slots:
    void showFrame(const QImage& frame);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage frame_;
};
