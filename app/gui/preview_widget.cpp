#include "preview_widget.h"

#include <QGenericMatrix>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QVector3D>

namespace {

// GLSL 1.10 / GLSL ES 1.00, the common subset of every context Qt may give us.
const char* kVertexShader = R"(
attribute vec2 position;
varying vec2 coord;
void main() {
    coord = vec2(position.x + 1.0, 1.0 - position.y) * 0.5;  // first texture row at the top
    gl_Position = vec4(position, 0.0, 1.0);
}
)";

// %1: the chroma texture's (U, V) channels, "rg" or "ra" for a luminance-alpha texture.
const char* kFragmentShader = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec2 coord;
uniform sampler2D luma;
uniform sampler2D chroma;
uniform mat3 yuv_to_rgb;
uniform vec3 yuv_offset;
void main() {
    vec3 yuv = vec3(texture2D(luma, coord).r, texture2D(chroma, coord).%1);
    gl_FragColor = vec4(yuv_to_rgb * (yuv - yuv_offset), 1.0);
}
)";

// Y'CbCr -> R'G'B' for the frame's matrix and range (ITU-R BT.709 / BT.601).
QMatrix3x3 conversion_matrix(const djivcam::media::Nv12Frame& frame) {
    const float kr = frame.bt709 ? 0.2126f : 0.299f;
    const float kb = frame.bt709 ? 0.0722f : 0.114f;
    const float kg = 1.0f - kr - kb;
    const float y = frame.full_range ? 1.0f : 255.0f / 219.0f;
    const float c = frame.full_range ? 1.0f : 255.0f / 224.0f;
    const float rows[9] = {
        y, 0.0f, c * 2.0f * (1.0f - kr),
        y, -c * 2.0f * kb * (1.0f - kb) / kg, -c * 2.0f * kr * (1.0f - kr) / kg,
        y, c * 2.0f * (1.0f - kb), 0.0f,
    };
    return QMatrix3x3(rows);
}

QVector3D conversion_offset(const djivcam::media::Nv12Frame& frame) {
    return {frame.full_range ? 0.0f : 16.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f};
}

}  // namespace

PreviewWidget::PreviewWidget(QWidget* parent) : QOpenGLWidget(parent) { setMinimumSize(320, 180); }

PreviewWidget::~PreviewWidget() { release_gl(); }

void PreviewWidget::showFrame(Frame frame) {
    frame_ = std::move(frame);
    uploaded_ = false;
    update();
}

void PreviewWidget::clear() {
    frame_.reset();
    update();
}

void PreviewWidget::initializeGL() {
    initializeOpenGLFunctions();
    luminance_textures_ = context()->format().majorVersion() < 3;
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &PreviewWidget::release_gl, Qt::UniqueConnection);

    program_ = std::make_unique<QOpenGLShaderProgram>();
    program_->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    program_->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                      QString::fromLatin1(kFragmentShader).arg(luminance_textures_ ? "ra" : "rg"));
    program_->bindAttributeLocation("position", 0);
    if (!program_->link()) {
        qWarning("preview shader: %s", qPrintable(program_->log()));
    }

    const GLfloat corners[] = {-1.0f, -1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f};  // triangle strip
    quad_.create();
    quad_.bind();
    quad_.allocate(corners, sizeof(corners));
    quad_.release();

    glGenTextures(2, textures_);
    for (GLuint texture : textures_) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    texture_width_ = texture_height_ = 0;
    uploaded_ = false;
}

void PreviewWidget::release_gl() {
    if (!textures_[0]) {
        return;
    }
    makeCurrent();
    glDeleteTextures(2, textures_);
    textures_[0] = textures_[1] = 0;
    quad_.destroy();
    program_.reset();
    doneCurrent();
}

void PreviewWidget::upload() {
    const int width = frame_->width;
    const int height = frame_->height;
    const GLenum luma_format = luminance_textures_ ? GL_LUMINANCE : GL_RED;
    const GLenum chroma_format = luminance_textures_ ? GL_LUMINANCE_ALPHA : GL_RG;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (width != texture_width_ || height != texture_height_) {
        glBindTexture(GL_TEXTURE_2D, textures_[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, luminance_textures_ ? GL_LUMINANCE : GL_R8, width, height, 0, luma_format,
                     GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, textures_[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, luminance_textures_ ? GL_LUMINANCE_ALPHA : GL_RG8, width / 2, height / 2, 0,
                     chroma_format, GL_UNSIGNED_BYTE, nullptr);
        texture_width_ = width;
        texture_height_ = height;
    }
    const std::uint8_t* luma = frame_->data.data();
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, luma_format, GL_UNSIGNED_BYTE, luma);
    glBindTexture(GL_TEXTURE_2D, textures_[1]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width / 2, height / 2, chroma_format, GL_UNSIGNED_BYTE,
                    luma + static_cast<std::size_t>(width) * height);
    uploaded_ = true;
}

void PreviewWidget::paintGL() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!frame_ || frame_->width < 2 || frame_->height < 2) {
        QPainter painter(this);
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, tr("No video"));
        return;
    }
    if (!uploaded_) {
        upload();
    }
    // Letterbox in device pixels (the framebuffer's unit on high-DPI screens).
    const qreal ratio = devicePixelRatioF();
    const QSize area(qRound(width() * ratio), qRound(height() * ratio));
    const QSize fitted = QSize(frame_->width, frame_->height).scaled(area, Qt::KeepAspectRatio);
    glViewport((area.width() - fitted.width()) / 2, (area.height() - fitted.height()) / 2, fitted.width(), fitted.height());
    draw_frame();
}

QImage PreviewWidget::snapshot() {
    if (!frame_ || !textures_[0] || !program_) {
        return {};
    }
    makeCurrent();
    if (!uploaded_) {
        upload();
    }
    QOpenGLFramebufferObject target(frame_->width, frame_->height);
    target.bind();
    glViewport(0, 0, frame_->width, frame_->height);
    draw_frame();
    QImage image = target.toImage();
    target.release();
    doneCurrent();
    return image;
}

void PreviewWidget::draw_frame() {
    program_->bind();
    program_->setUniformValue("luma", 0);
    program_->setUniformValue("chroma", 1);
    program_->setUniformValue("yuv_to_rgb", conversion_matrix(*frame_));
    program_->setUniformValue("yuv_offset", conversion_offset(*frame_));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, textures_[1]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures_[0]);
    quad_.bind();
    program_->enableAttributeArray(0);
    program_->setAttributeBuffer(0, GL_FLOAT, 0, 2);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    program_->disableAttributeArray(0);
    quad_.release();
    program_->release();
}
