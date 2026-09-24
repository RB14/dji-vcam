// Shows the latest live-view frame, letterboxed to keep its aspect ratio. The decoder's NV12
// frame goes to the GPU as is (a luma and a chroma texture) and a shader turns it into RGB while
// drawing, so neither the color conversion nor the scaling to the window costs CPU time.
#pragma once

#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>

#include <memory>

#include "djivcam/decoder.h"

class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    using Frame = std::shared_ptr<const djivcam::media::Nv12Frame>;

    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    void showFrame(Frame frame);
    void clear();
    // The current frame at its own resolution, drawn by the same shader as the preview (null
    // without video).
    QImage snapshot();

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    void upload();
    // Draws the frame over the whole current viewport.
    void draw_frame();
    void release_gl();

    Frame frame_;
    bool uploaded_ = false;  // frame_ is in the textures
    std::unique_ptr<QOpenGLShaderProgram> program_;  // per GL context: recreated with it
    QOpenGLBuffer quad_{QOpenGLBuffer::VertexBuffer};
    GLuint textures_[2] = {0, 0};  // luma (width x height), chroma (width/2 x height/2, 2 channels)
    int texture_width_ = 0;
    int texture_height_ = 0;
    bool luminance_textures_ = false;  // OpenGL (ES) 2 has no one/two-channel formats
};
