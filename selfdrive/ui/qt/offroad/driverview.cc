#include "selfdrive/ui/qt/offroad/driverview.h"

#include <QPainter>

#include "selfdrive/ui/qt/qt_window.h"
#include "selfdrive/ui/qt/util.h"

const int FACE_IMG_SIZE = 130;
bool infill = false;

DriverViewWindow::DriverViewWindow(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent);
  layout = new QStackedLayout(this);
  layout->setStackingMode(QStackedLayout::StackAll);

  cameraView = new CameraWidget("camerad", VISION_STREAM_DRIVER, true, this);
  layout->addWidget(cameraView);

  scene = new DriverViewScene(this);
  connect(cameraView, &CameraWidget::vipcThreadFrameReceived, scene, &DriverViewScene::frameUpdated);
  layout->addWidget(scene);
  layout->setCurrentWidget(scene);
}

void DriverViewWindow::mousePressEvent(QMouseEvent* e) {
  if (d_rec_btn.contains(e->pos())) {
    infill = !infill;
    if (infill) {

    } else {

    }
    return;
  }
  cameraView->stopVipcThread();
  emit done();
}

DriverViewScene::DriverViewScene(QWidget* parent) : sm({"driverStateV2"}), QWidget(parent) {
  face_img = loadPixmap("../assets/img_driver_face_static.png", {FACE_IMG_SIZE, FACE_IMG_SIZE});
}

void DriverViewScene::showEvent(QShowEvent* event) {
  frame_updated = false;
  params.putBool("IsDriverViewEnabled", true);
}

void DriverViewScene::hideEvent(QHideEvent* event) {
  params.putBool("IsDriverViewEnabled", false);
}

void DriverViewScene::frameUpdated() {
  frame_updated = true;
  sm.update(0);
  update();
}

void DriverViewScene::paintEvent(QPaintEvent* event) {
  QPainter p(this);

  // startup msg
  if (!frame_updated) {
    p.setPen(Qt::white);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(InterFont(100, QFont::Bold));
    p.drawText(geometry(), Qt::AlignCenter, tr("camera starting"));
    return;
  }

  cereal::DriverStateV2::Reader driver_state = sm["driverStateV2"].getDriverStateV2();
  cereal::DriverStateV2::DriverData::Reader driver_data;

  is_rhd = driver_state.getWheelOnRightProb() > 0.5;
  driver_data = is_rhd ? driver_state.getRightDriverData() : driver_state.getLeftDriverData();

  bool face_detected = driver_data.getFaceProb() > 0.7;
  if (face_detected) {
    auto fxy_list = driver_data.getFacePosition();
    auto std_list = driver_data.getFaceOrientationStd();
    float face_x = fxy_list[0];
    float face_y = fxy_list[1];
    float face_std = std::max(std_list[0], std_list[1]);

    float alpha = 0.7;
    if (face_std > 0.15) {
      alpha = std::max(0.7 - (face_std-0.15)*3.5, 0.0);
    }
    const int box_size = 220;
    // use approx instead of distort_points
    int fbox_x = 1080.0 - 1714.0 * face_x;
    int fbox_y = -135.0 + (504.0 + std::abs(face_x)*112.0) + (1205.0 - std::abs(face_x)*724.0) * face_y;
    p.setPen(QPen(QColor(255, 255, 255, alpha * 255), 10));
    p.drawRoundedRect(fbox_x - box_size / 2, fbox_y - box_size / 2, box_size, box_size, 35.0, 35.0);
  }

  // icon
  const int img_offset = 60;
  const int img_x = is_rhd ? rect().right() - FACE_IMG_SIZE - img_offset : rect().left() + img_offset;
  const int img_y = rect().bottom() - FACE_IMG_SIZE - img_offset;
  p.setOpacity(face_detected ? 1.0 : 0.2);
  p.drawPixmap(img_x, img_y, face_img);

  // opkr
  if (frame_updated) {
    p.setPen(QColor(0xff, 0xff, 0xff));
    p.setOpacity(1.0);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(InterFont(50, QFont::Bold));

    p.drawText(1050, 50, "faceProb:  " + QString::number(driver_data.getFaceProb(), 'f', 2));

    p.drawText(1050, 150, "leftEyeProb:  " + QString::number(driver_data.getLeftEyeProb(), 'f', 2));
    p.drawText(1050, 200, "rightEyeProb:  " + QString::number(driver_data.getRightEyeProb(), 'f', 2));
    p.drawText(1050, 250, "leftBlinkProb:  " + QString::number(driver_data.getLeftBlinkProb(), 'f', 2));
    p.drawText(1050, 300, "rightBlinkProb:  " + QString::number(driver_data.getRightBlinkProb(), 'f', 2));
    p.drawText(1050, 400, "sunglassesProb:  " + QString::number(driver_data.getSunglassesProb(), 'f', 2));

    QRect rec = {1985, 905, 140, 140};
    p.setBrush(Qt::NoBrush);
    if (infill) p.setBrush(Qt::red);
    p.setPen(QPen(QColor(255, 255, 255, 80), 6));
    p.drawEllipse(rec);
    p.setPen(QColor(255, 255, 255, 200));
    p.drawText(rec, Qt::AlignCenter, QString("REC"));
  }
}
