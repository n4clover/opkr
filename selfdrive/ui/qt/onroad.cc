#include "selfdrive/ui/qt/onroad.h"

#include <cmath>

#include <QDebug>
#include <QFileInfo>
#include <QDateTime>

#include "common/timing.h"
#include "selfdrive/ui/qt/util.h"
#include "selfdrive/ui/qt/widgets/input.h"
#ifdef ENABLE_MAPS
#include "selfdrive/ui/qt/maps/map.h"
#include "selfdrive/ui/qt/maps/map_helpers.h"
#endif

OnroadWindow::OnroadWindow(QWidget *parent) : QWidget(parent) {
  QVBoxLayout *main_layout  = new QVBoxLayout(this);
  main_layout->setMargin(bdr_s);
  QStackedLayout *stacked_layout = new QStackedLayout;
  stacked_layout->setStackingMode(QStackedLayout::StackAll);
  main_layout->addLayout(stacked_layout);

  nvg = new AnnotatedCameraWidget(VISION_STREAM_ROAD, this);

  QWidget * split_wrapper = new QWidget;
  split = new QHBoxLayout(split_wrapper);
  split->setContentsMargins(0, 0, 0, 0);
  split->setSpacing(0);
  split->addWidget(nvg);

  if (getenv("DUAL_CAMERA_VIEW")) {
    CameraWidget *arCam = new CameraWidget("camerad", VISION_STREAM_ROAD, true, this);
    split->insertWidget(0, arCam);
  }

  if (getenv("MAP_RENDER_VIEW")) {
    CameraWidget *map_render = new CameraWidget("navd", VISION_STREAM_MAP, false, this);
    split->insertWidget(0, map_render);
  }

  stacked_layout->addWidget(split_wrapper);

  alerts = new OnroadAlerts(this);
  alerts->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  stacked_layout->addWidget(alerts);

  // setup stacking order
  alerts->raise();

  setAttribute(Qt::WA_OpaquePaintEvent);
  QObject::connect(uiState(), &UIState::uiUpdate, this, &OnroadWindow::updateState);
  QObject::connect(uiState(), &UIState::offroadTransition, this, &OnroadWindow::offroadTransition);
}

void OnroadWindow::updateState(const UIState &s) {
  QColor bgColor = bg_colors[s.status];
  Alert alert = Alert::get(*(s.sm), s.scene.started_frame);
  if (s.sm->updated("controlsState") || !alert.equal({})) {
    if (alert.type == "controlsUnresponsive") {
      bgColor = bg_colors[STATUS_ALERT];
    } else if (alert.type == "controlsUnresponsivePermanent") {
      bgColor = bg_colors[STATUS_DISENGAGED];
    }
    if (!uiState()->is_OpenpilotViewEnabled) {
      // opkr
      if (QFileInfo::exists("/data/log/error.txt") && uiState()->scene.show_error && !uiState()->scene.tmux_error_check) {
        QFileInfo fileInfo;
        fileInfo.setFile("/data/log/error.txt");
        QDateTime modifiedtime = fileInfo.lastModified();
        QString modified_time = modifiedtime.toString("yyyy-MM-dd hh:mm:ss ");
        const std::string txt = util::read_file("/data/log/error.txt");
        if (RichTextDialog::alert(modified_time + QString::fromStdString(txt), this)) {
          uiState()->scene.tmux_error_check = true;
        }
      }
    alerts->updateAlert(alert, bgColor);
    }
    //printf("OPVIEW: %s\n", uiState()->is_OpenpilotViewEnabled ? "true" : "false");
  }

  if (s.scene.map_on_left) {
    split->setDirection(QBoxLayout::LeftToRight);
  } else {
    split->setDirection(QBoxLayout::RightToLeft);
  }

  nvg->updateState(s);

  if (bg != bgColor) {
    // repaint border
    bg = bgColor;
    update();
  }
}

void OnroadWindow::mousePressEvent(QMouseEvent* e) {

  QRect rec_btn = QRect(1985, 905, 140, 140);
  QRect laneless_btn = QRect(1825, 905, 140, 140);
  QRect monitoring_btn = QRect(50, 770, 140, 150);
  QRect stockui_btn = QRect(15, 15, 184, 202);
  QRect tuneui_btn = QRect(1960, 15, 184, 202);
  QRect speedlimit_btn = QRect(220, 15, 190, 190);

  if (rec_btn.contains(e->pos()) || laneless_btn.contains(e->pos()) || monitoring_btn.contains(e->pos()) || speedlimit_btn.contains(e->pos()) ||
    stockui_btn.contains(e->pos()) || tuneui_btn.contains(e->pos()) || uiState()->scene.live_tune_panel_enable) {return;}
  if (map != nullptr) {
    bool sidebarVisible = geometry().x() > 0;
    map->setVisible(!sidebarVisible && !map->isVisible());
    if (map->isVisible()) {
      uiState()->scene.mapbox_running = true;
    } else {
      uiState()->scene.mapbox_running = false;
    }
  }
  // propagation event to parent(HomeWindow)
  QWidget::mousePressEvent(e);
}

void OnroadWindow::offroadTransition(bool offroad) {
#ifdef ENABLE_MAPS
  if (!offroad) {
    if (map == nullptr && (uiState()->primeType() || !MAPBOX_TOKEN.isEmpty())) {
      MapWindow * m = new MapWindow(get_mapbox_settings());
      map = m;

      QObject::connect(uiState(), &UIState::offroadTransition, m, &MapWindow::offroadTransition);

      m->setFixedWidth(topWidget(this)->width() / 2 - bdr_s);
      split->insertWidget(0, m);

      // Make map visible after adding to split
      m->offroadTransition(offroad);
    }
  }
#endif

  alerts->updateAlert({}, bg);
}

void OnroadWindow::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.fillRect(rect(), QColor(bg.red(), bg.green(), bg.blue(), 255));
}

// ***** onroad widgets *****

// OnroadAlerts
void OnroadAlerts::updateAlert(const Alert &a, const QColor &color) {
  if (!alert.equal(a) || color != bg) {
    alert = a;
    bg = color;
    update();
  }
}

void OnroadAlerts::paintEvent(QPaintEvent *event) {
  if (alert.size == cereal::ControlsState::AlertSize::NONE) {
    return;
  }
  static std::map<cereal::ControlsState::AlertSize, const int> alert_sizes = {
    {cereal::ControlsState::AlertSize::SMALL, 271},
    {cereal::ControlsState::AlertSize::MID, 420},
    {cereal::ControlsState::AlertSize::FULL, height()},
  };
  int h = alert_sizes[alert.size];
  QRect r = QRect(0, height() - h, width(), h);

  QPainter p(this);

  // draw background + gradient
  p.setPen(Qt::NoPen);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);

  p.setBrush(QBrush(bg));
  p.drawRect(r);

  QLinearGradient g(0, r.y(), 0, r.bottom());
  g.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.05));
  g.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0.35));

  p.setCompositionMode(QPainter::CompositionMode_DestinationOver);
  p.setBrush(QBrush(g));
  p.fillRect(r, g);
  p.setCompositionMode(QPainter::CompositionMode_SourceOver);

  // text
  const QPoint c = r.center();
  p.setPen(QColor(0xff, 0xff, 0xff));
  p.setRenderHint(QPainter::TextAntialiasing);
  if (alert.size == cereal::ControlsState::AlertSize::SMALL) {
    configFont(p, "Inter", 74, "SemiBold");
    p.drawText(r, Qt::AlignCenter, alert.text1);
  } else if (alert.size == cereal::ControlsState::AlertSize::MID) {
    configFont(p, "Inter", 88, "Bold");
    p.drawText(QRect(0, c.y() - 125, width(), 150), Qt::AlignHCenter | Qt::AlignTop, alert.text1);
    configFont(p, "Inter", 66, "Regular");
    p.drawText(QRect(0, c.y() + 21, width(), 90), Qt::AlignHCenter, alert.text2);
  } else if (alert.size == cereal::ControlsState::AlertSize::FULL) {
    bool l = alert.text1.length() > 15;
    configFont(p, "Inter", l ? 132 : 177, "Bold");
    p.drawText(QRect(0, r.y() + (l ? 240 : 270), width(), 600), Qt::AlignHCenter | Qt::TextWordWrap, alert.text1);
    configFont(p, "Inter", 88, "Regular");
    p.drawText(QRect(0, r.height() - (l ? 361 : 420), width(), 300), Qt::AlignHCenter | Qt::TextWordWrap, alert.text2);
  }
}


ExperimentalButton::ExperimentalButton(QWidget *parent) : QPushButton(parent) {
  setVisible(false);
  setFixedSize(btn_size, btn_size);
  setCheckable(true);

  params = Params();
  engage_img = loadPixmap("../assets/img_chffr_wheel.png", {img_size, img_size});
  experimental_img = loadPixmap("../assets/img_experimental.svg", {img_size, img_size});

  QObject::connect(this, &QPushButton::toggled, [=](bool checked) {
    params.putBool("ExperimentalMode", checked);
  });
}

void ExperimentalButton::updateState(const UIState &s) {
  const SubMaster &sm = *(s.sm);

  // button is "visible" if engageable or enabled
  const auto cs = sm["controlsState"].getControlsState();
  setVisible(cs.getEngageable() || cs.getEnabled());

  // button is "checked" if experimental mode is enabled
  setChecked(sm["controlsState"].getControlsState().getExperimentalMode());

  // disable button when experimental mode is not available, or has not been confirmed for the first time
  const auto cp = sm["carParams"].getCarParams();
  const bool experimental_mode_available = cp.getExperimentalLongitudinalAvailable() ? params.getBool("ExperimentalLongitudinalEnabled") : cp.getOpenpilotLongitudinalControl();
  setEnabled(params.getBool("ExperimentalModeConfirmed") && experimental_mode_available);

  setProperty("engaged", cs.getEnabled());
  setProperty("ang_str", s.scene.angleSteers);
  setProperty("gear_shifter", int(s.scene.getGearShifter));
}

void ExperimentalButton::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  //QPoint center(btn_size / 2, btn_size / 2);
  QPixmap img = isChecked() ? experimental_img : engage_img;

  // engage-ability icon
  if (engaged) {
    drawIcon(p, rect().right() - radius / 2 - bdr_s, radius / 2 + bdr_s, img, 1.0, true, ang_str);
  } else if (!comma_stock_ui) {
    QString gear_text = "";
    switch(gear_shifter) {
      case 1 : gear_text = "P"; p.setPen(QColor(200, 200, 255, 255)); break;
      case 2 : gear_text = "D"; p.setPen(greenColor(255)); break;
      case 3 : gear_text = "N"; p.setPen(whiteColor(255)); break;
      case 4 : gear_text = "R"; p.setPen(redColor(255)); break;
      case 5 : gear_text = "M"; p.setPen(greenColor(255)); break;
      case 7 : gear_text = "B"; p.setPen(whiteColor(255)); break;
      default: gear_text = QString::number(gear_shifter, 'f', 0); p.setPen(whiteColor(255)); break;
    }
    debugText(p, rect().right() - radius / 2 - bdr_s, radius / 2 + bdr_s + 70, gear_text, 255, 190, true);
  }
}

void ExperimentalButton::drawIcon(QPainter &p, int x, int y, QPixmap &img, float opacity, bool rotation, float angle) {
  // opkr
  if (rotation) {
    p.setOpacity(opacity);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 166));
    p.drawEllipse(x - radius / 2, y - radius / 2, radius, radius);
    p.setOpacity(isDown() ? 0.8 : 1.0);
    p.save();
    p.translate(x, y);
    p.rotate(-angle);
    QRect r = img.rect();
    r.moveCenter(QPoint(0,0));
    p.drawPixmap(r, img);
    p.restore();
  } else {
    p.setOpacity(opacity);
    p.setPen(Qt::NoPen);
    p.drawEllipse(x - radius / 2, y - radius / 2, radius, radius);
    p.drawPixmap(x - img_size / 2, y - img_size / 2, img);
  }
}

void ExperimentalButton::debugText(QPainter &p, int x, int y, const QString &text, int alpha, int fontsize, bool bold) {
  if (bold) {
    configFont(p, "Inter", fontsize, "Bold");
  } else {
    configFont(p, "Inter", fontsize, "SemiBold");
  }
  QFontMetrics fm(p.font());
  QRect init_rect = fm.boundingRect(text);
  QRect real_rect = fm.boundingRect(init_rect, 0, text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});

  //p.setPen(QColor(0xff, 0xff, 0xff, alpha));
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}

AnnotatedCameraWidget::AnnotatedCameraWidget(VisionStreamType type, QWidget* parent) : fps_filter(UI_FREQ, 3, 1. / UI_FREQ), CameraWidget("camerad", type, true, parent) {
  pm = std::make_unique<PubMaster, const std::initializer_list<const char *>>({"uiDebug"});

  QVBoxLayout *main_layout  = new QVBoxLayout(this);
  main_layout->setMargin(bdr_s);
  main_layout->setSpacing(0);

  experimental_btn = new ExperimentalButton(this);
  main_layout->addWidget(experimental_btn, 0, Qt::AlignTop | Qt::AlignRight);

  dm_img = loadPixmap("../assets/img_driver_face.png", {img_size + 5, img_size + 5});
}

void AnnotatedCameraWidget::updateState(const UIState &s) {
  const int SET_SPEED_NA = 255;
  const SubMaster &sm = *(s.sm);

  const bool cs_alive = sm.alive("controlsState");
  const bool nav_alive = sm.alive("navInstruction") && sm["navInstruction"].getValid();

  const auto cs = sm["controlsState"].getControlsState();

  // Handle older routes where vCruiseCluster is not set
  float v_cruise =  cs.getVCruiseCluster() == 0.0 ? cs.getVCruise() : cs.getVCruiseCluster();
  float set_speed = cs_alive ? v_cruise : SET_SPEED_NA;
  bool cruise_set = set_speed > 0 && (int)set_speed != SET_SPEED_NA;
  if (cruise_set && !s.scene.is_metric) {
    set_speed *= KM_TO_MILE;
  }

  // Handle older routes where vEgoCluster is not set
  float v_ego;
  if (sm["carState"].getCarState().getVEgoCluster() == 0.0 && !v_ego_cluster_seen) {
    v_ego = sm["carState"].getCarState().getVEgo();
  } else {
    v_ego = sm["carState"].getCarState().getVEgoCluster();
    v_ego_cluster_seen = true;
  }
  float cur_speed = cs_alive ? std::max<float>(0.0, v_ego) : 0.0;
  cur_speed *= s.scene.is_metric ? MS_TO_KPH : MS_TO_MPH;

  auto speed_limit_sign = sm["navInstruction"].getNavInstruction().getSpeedLimitSign();
  float speed_limit = nav_alive ? sm["navInstruction"].getNavInstruction().getSpeedLimit() : 0.0;
  speed_limit *= (s.scene.is_metric ? MS_TO_KPH : MS_TO_MPH);

  setProperty("speedLimit", speed_limit);
  setProperty("has_us_speed_limit", nav_alive && speed_limit_sign == cereal::NavInstruction::SpeedLimitSign::MUTCD);
  setProperty("has_eu_speed_limit", nav_alive && speed_limit_sign == cereal::NavInstruction::SpeedLimitSign::VIENNA);

  setProperty("is_cruise_set", cruise_set);
  setProperty("is_metric", s.scene.is_metric);
  setProperty("speed", cur_speed);
  setProperty("setSpeed", set_speed);
  setProperty("speedUnit", s.scene.is_metric ? tr("km/h") : tr("mph"));
  setProperty("hideDM", (cs.getAlertSize() != cereal::ControlsState::AlertSize::NONE));
  setProperty("status", s.status);

  // update engageability/experimental mode button
  experimental_btn->updateState(s);

  // update DM icon
  auto dm_state = sm["driverMonitoringState"].getDriverMonitoringState();
  setProperty("dmActive", dm_state.getIsActiveMode());
  setProperty("rightHandDM", dm_state.getIsRHD());
  // DM icon transition
  dm_fade_state = std::clamp(dm_fade_state+0.2*(0.5-dmActive), 0.0, 1.0);


  // opkr
  float cruisespeed = s.scene.vSetDis;
  bool over_sl = false;
  bool comma_ui = s.scene.comma_stock_ui;

  if (s.scene.navi_select == 2) {
    over_sl = s.scene.limitSpeedCamera > 21 && ((s.scene.car_state.getVEgo() * (s.scene.is_metric ? MS_TO_KPH : MS_TO_MPH)) > s.scene.ctrl_speed+1.5);
  } else if (s.scene.navi_select == 1 && (s.scene.mapSign != 20 && s.scene.mapSign != 21)) {
    over_sl = s.scene.limitSpeedCamera > 21 && ((s.scene.car_state.getVEgo() * (s.scene.is_metric ? MS_TO_KPH : MS_TO_MPH)) > s.scene.ctrl_speed+1.5);
  }

  auto lead_one = sm["radarState"].getRadarState().getLeadOne();
  float drel = lead_one.getDRel();
  float vrel = lead_one.getVRel();
  bool leadstat = lead_one.getStatus();

  setProperty("cruiseSpeed", cruisespeed);
  setProperty("is_over_sl", over_sl);
  setProperty("comma_stock_ui", comma_ui);
  setProperty("lead_stat", leadstat);
  setProperty("dist_rel", drel);
  setProperty("vel_rel", vrel);
  setProperty("ang_str", s.scene.angleSteers);
  setProperty("record_stat", s.scene.rec_stat);
  setProperty("lane_stat", s.scene.laneless_mode);
  setProperty("laneless_stat", s.scene.lateralPlan.lanelessModeStatus);
  setProperty("mapbox_stat", s.scene.mapbox_running);
  setProperty("dm_mode", s.scene.monitoring_mode);
  setProperty("ss_elapsed", s.scene.lateralPlan.standstillElapsedTime);
  setProperty("standstill", s.scene.standStill);
  setProperty("auto_hold", s.scene.autoHold);
  setProperty("left_blinker", s.scene.leftBlinker);
  setProperty("right_blinker", s.scene.rightBlinker);
  setProperty("blinker_rate", s.scene.blinker_blinkingrate);
  setProperty("a_req_v", s.scene.a_req_value);
  setProperty("brake_pressed", s.scene.brakePress);
  setProperty("brake_light", s.scene.brakeLights);
  setProperty("gas_pressed", s.scene.gasPress);
  setProperty("safety_speed", s.scene.limitSpeedCamera);
  setProperty("safety_dist", s.scene.limitSpeedCameraDist);
  setProperty("decel_off", s.scene.sl_decel_off);
}

void AnnotatedCameraWidget::drawHud(QPainter &p) {
  p.save();

  // Header gradient
  QLinearGradient bg(0, header_h - (header_h / 2.5), 0, header_h);
  bg.setColorAt(0, QColor::fromRgbF(0, 0, 0, 0.45));
  bg.setColorAt(1, QColor::fromRgbF(0, 0, 0, 0));
  p.fillRect(0, 0, width(), header_h, bg);

  QString speedLimitStr = (speedLimit > 1) ? QString::number(std::nearbyint(speedLimit)) : "–";
  QString speedStr = QString::number(std::nearbyint(speed));
  QString setSpeedStr = is_cruise_set ? QString::number(std::nearbyint(setSpeed)) : "–";

  // Draw outer box + border to contain set speed and speed limit
  int default_rect_width = 172;
  int rect_width = default_rect_width;
  if (is_metric || has_eu_speed_limit) rect_width = 200;
  if (has_us_speed_limit && speedLimitStr.size() >= 3) rect_width = 223;

  int rect_height = 204;
  if (has_us_speed_limit) rect_height = 402;
  else if (has_eu_speed_limit) rect_height = 392;

  int top_radius = 32;
  int bottom_radius = has_eu_speed_limit ? 100 : 32;

  QRect set_speed_rect(60 + default_rect_width / 2 - rect_width / 2, 45, rect_width, rect_height);
  p.setPen(QPen(whiteColor(75), 6));
  p.setBrush(blackColor(166));
  drawRoundedRect(p, set_speed_rect, top_radius, top_radius, bottom_radius, bottom_radius);

  // Draw MAX
  if (is_cruise_set) {
    if (status == STATUS_DISENGAGED) {
      p.setPen(whiteColor());
    } else if (status == STATUS_OVERRIDE) {
      p.setPen(QColor(0x91, 0x9b, 0x95, 0xff));
    } else if (speedLimit > 0) {
      p.setPen(interpColor(
        setSpeed,
        {speedLimit + 5, speedLimit + 15, speedLimit + 25},
        {QColor(0x80, 0xd8, 0xa6, 0xff), QColor(0xff, 0xe4, 0xbf, 0xff), QColor(0xff, 0xbf, 0xbf, 0xff)}
      ));
    } else {
      p.setPen(QColor(0x80, 0xd8, 0xa6, 0xff));
    }
  } else {
    p.setPen(QColor(0xa6, 0xa6, 0xa6, 0xff));
  }
  configFont(p, "Inter", 40, "SemiBold");
  QRect max_rect = getTextRect(p, Qt::AlignCenter, tr("MAX"));
  max_rect.moveCenter({set_speed_rect.center().x(), 0});
  max_rect.moveTop(set_speed_rect.top() + 27);
  p.drawText(max_rect, Qt::AlignCenter, tr("MAX"));

  // Draw set speed
  if (is_cruise_set) {
    if (speedLimit > 0 && status != STATUS_DISENGAGED && status != STATUS_OVERRIDE) {
      p.setPen(interpColor(
        setSpeed,
        {speedLimit + 5, speedLimit + 15, speedLimit + 25},
        {whiteColor(), QColor(0xff, 0x95, 0x00, 0xff), QColor(0xff, 0x00, 0x00, 0xff)}
      ));
    } else {
      p.setPen(whiteColor());
    }
  } else {
    p.setPen(QColor(0x72, 0x72, 0x72, 0xff));
  }
  configFont(p, "Inter", 90, "Bold");
  QRect speed_rect = getTextRect(p, Qt::AlignCenter, setSpeedStr);
  speed_rect.moveCenter({set_speed_rect.center().x(), 0});
  speed_rect.moveTop(set_speed_rect.top() + 77);
  p.drawText(speed_rect, Qt::AlignCenter, setSpeedStr);



  // US/Canada (MUTCD style) sign
  if (has_us_speed_limit) {
    const int border_width = 6;
    const int sign_width = rect_width - 24;
    const int sign_height = 186;

    // White outer square
    QRect sign_rect_outer(set_speed_rect.left() + 12, set_speed_rect.bottom() - 11 - sign_height, sign_width, sign_height);
    p.setPen(Qt::NoPen);
    p.setBrush(whiteColor());
    p.drawRoundedRect(sign_rect_outer, 24, 24);

    // Smaller white square with black border
    QRect sign_rect(sign_rect_outer.left() + 1.5 * border_width, sign_rect_outer.top() + 1.5 * border_width, sign_width - 3 * border_width, sign_height - 3 * border_width);
    p.setPen(QPen(blackColor(), border_width));
    p.setBrush(whiteColor());
    p.drawRoundedRect(sign_rect, 16, 16);

    // "SPEED"
    configFont(p, "Inter", 28, "SemiBold");
    QRect text_speed_rect = getTextRect(p, Qt::AlignCenter, tr("SPEED"));
    text_speed_rect.moveCenter({sign_rect.center().x(), 0});
    text_speed_rect.moveTop(sign_rect_outer.top() + 22);
    p.drawText(text_speed_rect, Qt::AlignCenter, tr("SPEED"));

    // "LIMIT"
    QRect text_limit_rect = getTextRect(p, Qt::AlignCenter, tr("LIMIT"));
    text_limit_rect.moveCenter({sign_rect.center().x(), 0});
    text_limit_rect.moveTop(sign_rect_outer.top() + 51);
    p.drawText(text_limit_rect, Qt::AlignCenter, tr("LIMIT"));

    // Speed limit value
    configFont(p, "Inter", 70, "Bold");
    QRect speed_limit_rect = getTextRect(p, Qt::AlignCenter, speedLimitStr);
    speed_limit_rect.moveCenter({sign_rect.center().x(), 0});
    speed_limit_rect.moveTop(sign_rect_outer.top() + 85);
    p.drawText(speed_limit_rect, Qt::AlignCenter, speedLimitStr);
  }

  // EU (Vienna style) sign
  if (has_eu_speed_limit) {
    int outer_radius = 176 / 2;
    int inner_radius_1 = outer_radius - 6; // White outer border
    int inner_radius_2 = inner_radius_1 - 20; // Red circle

    // Draw white circle with red border
    QPoint center(set_speed_rect.center().x() + 1, set_speed_rect.top() + 204 + outer_radius);
    p.setPen(Qt::NoPen);
    p.setBrush(whiteColor());
    p.drawEllipse(center, outer_radius, outer_radius);
    p.setBrush(QColor(255, 0, 0, 255));
    p.drawEllipse(center, inner_radius_1, inner_radius_1);
    p.setBrush(whiteColor());
    p.drawEllipse(center, inner_radius_2, inner_radius_2);

    // Speed limit value
    int font_size = (speedLimitStr.size() >= 3) ? 60 : 70;
    configFont(p, "Inter", font_size, "Bold");
    QRect speed_limit_rect = getTextRect(p, Qt::AlignCenter, speedLimitStr);
    speed_limit_rect.moveCenter(center);
    p.setPen(blackColor());
    p.drawText(speed_limit_rect, Qt::AlignCenter, speedLimitStr);
  }

  // current speed
  float gas_opacity = a_req_v*255>255?255:a_req_v*255;
  float brake_opacity = abs(a_req_v*175)>255?255:abs(a_req_v*175);
  if (brake_pressed && !comma_stock_ui) {
  	p.setPen(QColor(255, 0, 0, 255));
  } else if (brake_light && speedStr == "0" && !comma_stock_ui) {
    p.setPen(redColor(100));
  } else if (gas_pressed && !comma_stock_ui) {
    p.setPen(QColor(0, 240, 0, 255));
  } else if (a_req_v < 0 && !comma_stock_ui) {
    p.setPen(QColor((255-int(abs(a_req_v*8))), (255-int(brake_opacity)), (255-int(brake_opacity)), 255));
  } else if (a_req_v > 0 && !comma_stock_ui) {
    p.setPen(QColor((255-int(gas_opacity)), (255-int((a_req_v*10))), (255-int(gas_opacity)), 255));
  }
  configFont(p, "Inter", 176, "Bold");
  drawText(p, rect().center().x(), 210, speedStr);
  configFont(p, "Inter", 66, "Regular");
  drawText(p, rect().center().x(), 290, speedUnit, 200);


  // opkr
  UIState *s = uiState();

  p.setBrush(QColor(0, 0, 0, 0));
  p.setPen(whiteColor(150));
  //p.setRenderHint(QPainter::TextAntialiasing);
  p.setOpacity(0.7);
  int ui_viz_rx = bdr_s + 190;
  int ui_viz_ry = bdr_s + 100;
  int ui_viz_rx_center = s->fb_w/2;

  // debug
  int debug_y1 = 1010-bdr_s+(s->scene.mapbox_running ? 18:0)-(s->scene.animated_rpm?60:0);
  int debug_y2 = 1050-bdr_s+(s->scene.mapbox_running ? 3:0)-(s->scene.animated_rpm?60:0);
  int debug_y3 = 970-bdr_s+(s->scene.mapbox_running ? 18:0)-(s->scene.animated_rpm?60:0);
  if (s->scene.nDebugUi1 && !comma_stock_ui) {
    configFont(p, "Inter", s->scene.mapbox_running?20:25, "Semibold");
    uiText(p, 205, debug_y1, s->scene.alertTextMsg1.c_str());
    uiText(p, 205, debug_y2, s->scene.alertTextMsg2.c_str());
  }
  if (s->scene.nDebugUi3 && !comma_stock_ui) {
    uiText(p, 205, debug_y3, s->scene.alertTextMsg3.c_str());
  }
  if (s->scene.OPKR_Debug && s->scene.navi_select > 0 && !comma_stock_ui) {
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+240, "0: " + QString::fromStdString(s->scene.liveENaviData.eopkr0));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+280, "1: " + QString::fromStdString(s->scene.liveENaviData.eopkr1));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+320, "2: " + QString::fromStdString(s->scene.liveENaviData.eopkr2));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+360, "3: " + QString::fromStdString(s->scene.liveENaviData.eopkr3));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+400, "4: " + QString::fromStdString(s->scene.liveENaviData.eopkr4));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+440, "5: " + QString::fromStdString(s->scene.liveENaviData.eopkr5));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+480, "6: " + QString::fromStdString(s->scene.liveENaviData.eopkr6));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+520, "7: " + QString::fromStdString(s->scene.liveENaviData.eopkr7));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+560, "8: " + QString::fromStdString(s->scene.liveENaviData.eopkr8));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 300:400), ui_viz_ry+600, "9: " + QString::fromStdString(s->scene.liveENaviData.eopkr9));
  }
  if (s->scene.nDebugUi2 && !comma_stock_ui) {
    configFont(p, "Inter", s->scene.mapbox_running?26:35, "Semibold");
    uiText(p, ui_viz_rx, ui_viz_ry+240, "SR:" + QString::number(s->scene.liveParams.steerRatio, 'f', 2));
    uiText(p, ui_viz_rx, ui_viz_ry+280, "AA:" + QString::number(s->scene.liveParams.angleOffsetAverage, 'f', 2));
    uiText(p, ui_viz_rx, ui_viz_ry+320, "SF:" + QString::number(s->scene.liveParams.stiffnessFactor, 'f', 2));
    uiText(p, ui_viz_rx, ui_viz_ry+360, "AD:" + QString::number(s->scene.steer_actuator_delay, 'f', 2));
    uiText(p, ui_viz_rx, ui_viz_ry+400, "OS:" + QString::number(s->scene.output_scale, 'f', 2));
    uiText(p, ui_viz_rx, ui_viz_ry+440, QString::number(s->scene.lateralPlan.lProb, 'f', 2) + "|" + QString::number(s->scene.lateralPlan.rProb, 'f', 2));

    char const* szLaCMethod = nullptr;
    char const* szLaCMethodCur = nullptr;
    switch( s->scene.lateralControlMethod  )
      {
        case  0: szLaCMethod = "PID"; break;
        case  1: szLaCMethod = "INDI"; break;
        case  2: szLaCMethod = "LQR"; break;
        case  3: szLaCMethod = "TORQUE"; break;
        case  4: szLaCMethod = "MULTI"; break;
      }
    switch( (int)s->scene.multi_lat_selected  )
      {
        case  0: szLaCMethodCur = "PID"; break;
        case  1: szLaCMethodCur = "INDI"; break;
        case  2: szLaCMethodCur = "LQR"; break;
        case  3: szLaCMethodCur = "TORQUE"; break;
      }
    if ( !s->scene.animated_rpm )
    {
      if( szLaCMethod )
          drawText(p, ui_viz_rx_center, bdr_s+295, szLaCMethod );
      if (s->scene.lateralControlMethod == 4) {
        if( szLaCMethodCur )
            drawText(p, ui_viz_rx_center, bdr_s+330, szLaCMethodCur );
        }
    } else {
      if( szLaCMethod )
          drawText(p, ui_viz_rx_center, bdr_s+320, szLaCMethod );
      if (s->scene.lateralControlMethod == 4) {
        if( szLaCMethodCur )
            drawText(p, ui_viz_rx_center, bdr_s+355, szLaCMethodCur );
        }
    }
    if (s->scene.navi_select == 1) {
      if (s->scene.liveENaviData.eopkrsafetysign) uiText(p, ui_viz_rx, ui_viz_ry+560, "CS:" + QString::number(s->scene.liveENaviData.eopkrsafetysign, 'f', 0));
      if (s->scene.liveENaviData.eopkrspeedlimit) uiText(p, ui_viz_rx, ui_viz_ry+600, "SL:" + QString::number(s->scene.liveENaviData.eopkrspeedlimit, 'f', 0) + "/DS:" + QString::number(s->scene.liveENaviData.eopkrsafetydist, 'f', 0));
      if (s->scene.liveENaviData.eopkrturninfo) uiText(p, ui_viz_rx, ui_viz_ry+640, "TI:" + QString::number(s->scene.liveENaviData.eopkrturninfo, 'f', 0) + "/DT:" + QString::number(s->scene.liveENaviData.eopkrdisttoturn, 'f', 0));
      if (s->scene.liveENaviData.eopkrroadlimitspeed > 0 && s->scene.liveENaviData.eopkrroadlimitspeed < 200) uiText(p, ui_viz_rx, ui_viz_ry+680, "RS:" + QString::number(s->scene.liveENaviData.eopkrroadlimitspeed, 'f', 0));
      if (s->scene.liveENaviData.eopkrishighway || s->scene.liveENaviData.eopkristunnel) uiText(p, ui_viz_rx, ui_viz_ry+720, "H:" + QString::number(s->scene.liveENaviData.eopkrishighway, 'f', 0) + "/T:" + QString::number(s->scene.liveENaviData.eopkristunnel, 'f', 0));
      //if (scene.liveENaviData.eopkrlinklength || scene.liveENaviData.eopkrcurrentlinkangle || scene.liveENaviData.eopkrnextlinkangle) uiText(p, ui_viz_rx, ui_viz_ry+840, "L:%d/C:%d/N:%d", scene.liveENaviData.eopkrlinklength, scene.liveENaviData.eopkrcurrentlinkangle, scene.liveENaviData.eopkrnextlinkangle);
    } else if (s->scene.navi_select == 2) {
      if (s->scene.liveENaviData.ewazealertdistance) uiText(p, ui_viz_rx, ui_viz_ry+560, "AS:" + QString::number(s->scene.liveENaviData.ewazealertid, 'f', 0) + "/DS:" + QString::number(s->scene.liveENaviData.ewazealertdistance, 'f', 0));
      if (s->scene.liveENaviData.ewazealertdistance) uiText(p, ui_viz_rx, ui_viz_ry+600, "T:" + QString::fromStdString(s->scene.liveENaviData.ewazealerttype));
      if (s->scene.liveENaviData.ewazecurrentspeed || s->scene.liveENaviData.ewazeroadspeedlimit) uiText(p, ui_viz_rx, ui_viz_ry+640, "CS:" + QString::number(s->scene.liveENaviData.ewazecurrentspeed, 'f', 0) + "/RS:" + QString::number(s->scene.liveENaviData.ewazeroadspeedlimit, 'f', 0));
      if (s->scene.liveENaviData.ewazenavsign) uiText(p, ui_viz_rx, ui_viz_ry+680, "NS:" + QString::number(s->scene.liveENaviData.ewazenavsign, 'f', 0));
      if (s->scene.liveENaviData.ewazenavdistance) uiText(p, ui_viz_rx, ui_viz_ry+720, "ND:" + QString::number(s->scene.liveENaviData.ewazenavdistance, 'f', 0));
    }
    if (s->scene.osm_enabled && !s->scene.OPKR_Debug) {
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 150:200), ui_viz_ry+240, "SL:" + QString::number(s->scene.liveMapData.ospeedLimit, 'f', 0));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 150:200), ui_viz_ry+280, "SLA:" + QString::number(s->scene.liveMapData.ospeedLimitAhead, 'f', 0));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 150:200), ui_viz_ry+320, "SLAD:" + QString::number(s->scene.liveMapData.ospeedLimitAheadDistance, 'f', 0));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 150:200), ui_viz_ry+360, "TSL:" + QString::number(s->scene.liveMapData.oturnSpeedLimit, 'f', 0));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 150:200), ui_viz_ry+400, "TSLED:" + QString::number(s->scene.liveMapData.oturnSpeedLimitEndDistance, 'f', 0));
      uiText(p, ui_viz_rx+(s->scene.mapbox_running ? 150:200), ui_viz_ry+440, "TSLS:" + QString::number(s->scene.liveMapData.oturnSpeedLimitSign, 'f', 0));
    }
  }

  if (!comma_stock_ui) {
    int j_num = 100;
    // opkr debug info(left panel)
    int width_l = 180;
    int sp_xl = rect().left() + bdr_s + width_l / 2 - 10;
    int sp_yl = bdr_s + 260;
    int num_l = 4;
    if (s->scene.longitudinal_control) {num_l = num_l + 1;}
    QRect left_panel(rect().left() + bdr_s, bdr_s + 200, width_l, 104*num_l);  
    p.setOpacity(1.0);
    p.setPen(QPen(QColor(255, 255, 255, 80), 6));
    p.drawRoundedRect(left_panel, 20, 20);
    p.setPen(whiteColor(200));
    //p.setRenderHint(QPainter::TextAntialiasing);
    // lead drel
    if (lead_stat) {
      if (dist_rel < 5) {
        p.setPen(redColor(200));
      } else if (int(dist_rel) < 15) {
        p.setPen(orangeColor(200));
      }
      if (dist_rel < 10) {
        debugText(p, sp_xl, sp_yl, QString::number(dist_rel, 'f', 1), 150, 58);
      } else {
        debugText(p, sp_xl, sp_yl, QString::number(dist_rel, 'f', 0), 150, 58);
      }
    }
    p.setPen(whiteColor(200));
    debugText(p, sp_xl, sp_yl + 35, QString("REL DIST"), 150, 27);
    p.translate(sp_xl + 90, sp_yl + 20);
    p.rotate(-90);
    p.drawText(0, 0, "m");
    p.resetMatrix();
    // lead spd
    sp_yl = sp_yl + j_num;
    if (int(vel_rel) < -5) {
      p.setPen(redColor(200));
    } else if (int(vel_rel) < 0) {
      p.setPen(orangeColor(200));
    }
    if (lead_stat) {
      debugText(p, sp_xl, sp_yl, QString::number(vel_rel * (s->scene.is_metric ? 3.6 : 2.2369363), 'f', 0), 150, 58);
    } else {
      debugText(p, sp_xl, sp_yl, "-", 150, 58);
    }
    p.setPen(whiteColor(200));
    debugText(p, sp_xl, sp_yl + 35, QString("REL SPED"), 150, 27);
    p.translate(sp_xl + 90, sp_yl + 20);
    p.rotate(-90);
    if (s->scene.is_metric) {p.drawText(0, 0, "km/h");} else {p.drawText(0, 0, "mi/h");}
    p.resetMatrix();
    // steer angle
    sp_yl = sp_yl + j_num;
    p.setPen(greenColor(200));
    if ((int(ang_str) < -50) || (int(ang_str) > 50)) {
      p.setPen(redColor(200));
    } else if ((int(ang_str) < -30) || (int(ang_str) > 30)) {
      p.setPen(orangeColor(200));
    }
    debugText(p, sp_xl, sp_yl, QString::number(ang_str, 'f', 0), 150, 58);
    p.setPen(whiteColor(200));
    debugText(p, sp_xl, sp_yl + 35, QString("STER ANG"), 150, 27);
    p.translate(sp_xl + 90, sp_yl + 20);
    p.rotate(-90);
    p.drawText(0, 0, "       °");
    p.resetMatrix();
    // steer ratio
    sp_yl = sp_yl + j_num;
    debugText(p, sp_xl, sp_yl, QString::number(s->scene.steerRatio, 'f', 2), 150, 58);
    debugText(p, sp_xl, sp_yl + 35, QString("SteerRatio"), 150, 27);
    // cruise gap for long
    if (s->scene.longitudinal_control) {
      sp_yl = sp_yl + j_num;
      if (s->scene.controls_state.getEnabled()) {
        if (s->scene.cruise_gap == s->scene.dynamic_tr_mode) {
          debugText(p, sp_xl, sp_yl, "AUT", 150, 58);
        } else {
          debugText(p, sp_xl, sp_yl, QString::number(s->scene.cruise_gap, 'f', 0), 150, 58);
        }
      } else {
        debugText(p, sp_xl, sp_yl, "-", 150, 58);
      }
      debugText(p, sp_xl, sp_yl + 35, QString("CruiseGap"), 150, 27);
      if (s->scene.cruise_gap == s->scene.dynamic_tr_mode) {
        p.translate(sp_xl + 90, sp_yl + 20);
        p.rotate(-90);
        p.drawText(0, 0, QString::number(s->scene.dynamic_tr_value, 'f', 0));
        p.resetMatrix();
      }
    }

    // opkr debug info(right panel)
    int width_r = 180;
    int sp_xr = rect().right() - bdr_s - width_r / 2 - 10;
    int sp_yr = bdr_s + 260;
    int num_r = 1;
    num_r = num_r + 1;
    if (s->scene.gpsAccuracyUblox != 0.00) {num_r = num_r + 2;}
    QRect right_panel(rect().right() - bdr_s - width_r, bdr_s + 200, width_r, 104*num_r);  
    p.setOpacity(1.0);
    p.setPen(QPen(QColor(255, 255, 255, 80), 6));
    p.drawRoundedRect(right_panel, 20, 20);
    p.setPen(whiteColor(200));
    //p.setRenderHint(QPainter::TextAntialiasing);
    // cpu temp
    if (s->scene.cpuTemp > 85) {
      p.setPen(redColor(200));
    } else if (s->scene.cpuTemp > 75) {
      p.setPen(orangeColor(200));
    }
    debugText(p, sp_xr, sp_yr, QString::number(s->scene.cpuTemp, 'f', 0) + "°C", 150, 58);
    p.setPen(whiteColor(200));
    debugText(p, sp_xr, sp_yr + 35, QString("CPU TEMP"), 150, 27);
    p.translate(sp_xr + 90, sp_yr + 20);
    p.rotate(-90);
    p.drawText(0, 0, QString::number(s->scene.cpuPerc, 'f', 0) + "%");
    p.resetMatrix();
    // sys temp
    sp_yr = sp_yr + j_num;
    if (s->scene.ambientTemp > 50) {
      p.setPen(redColor(200));
    } else if (s->scene.ambientTemp > 45) {
      p.setPen(orangeColor(200));
    } 
    debugText(p, sp_xr, sp_yr, QString::number(s->scene.ambientTemp, 'f', 0) + "°C", 150, 58);
    p.setPen(whiteColor(200));
    debugText(p, sp_xr, sp_yr + 35, QString("SYS TEMP"), 150, 27);
    p.translate(sp_xr + 90, sp_yr + 20);
    p.rotate(-90);
    p.drawText(0, 0, QString::number(s->scene.fanSpeed/1000, 'f', 0));
    p.resetMatrix();
    // Ublox GPS accuracy
    if (s->scene.gpsAccuracyUblox != 0.00) {
      sp_yr = sp_yr + j_num;
      if (s->scene.gpsAccuracyUblox > 1.3) {
        p.setPen(redColor(200));
      } else if (s->scene.gpsAccuracyUblox > 0.85) {
        p.setPen(orangeColor(200));
      }
      if (s->scene.gpsAccuracyUblox > 99 || s->scene.gpsAccuracyUblox == 0) {
        debugText(p, sp_xr, sp_yr, "None", 150, 58);
      } else if (s->scene.gpsAccuracyUblox > 9.99) {
        debugText(p, sp_xr, sp_yr, QString::number(s->scene.gpsAccuracyUblox, 'f', 1), 150, 58);
      } else {
        debugText(p, sp_xr, sp_yr, QString::number(s->scene.gpsAccuracyUblox, 'f', 2), 150, 58);
      }
      p.setPen(whiteColor(200));
      debugText(p, sp_xr, sp_yr + 35, QString("GPS PREC"), 150, 27);
      p.translate(sp_xr + 90, sp_yr + 20);
      p.rotate(-90);
      p.drawText(0, 0, QString::number(s->scene.satelliteCount, 'f', 0));
      p.resetMatrix();
      // altitude
      sp_yr = sp_yr + j_num;
      debugText(p, sp_xr, sp_yr, QString::number(s->scene.altitudeUblox, 'f', 0), 150, 58);
      debugText(p, sp_xr, sp_yr + 35, QString("ALTITUDE"), 150, 27);
      p.translate(sp_xr + 90, sp_yr + 20);
      p.rotate(-90);
      p.drawText(0, 0, "m");
      p.resetMatrix();
    }

    // opkr tpms
    int tpms_width = 180;
    int tpms_sp_xr = rect().right() - bdr_s - tpms_width / 2;
    int tpms_sp_yr = sp_yr + j_num;
    QRect tpms_panel(rect().right() - bdr_s - tpms_width, tpms_sp_yr - 20, tpms_width, 130);  
    p.setOpacity(1.0);
    p.setPen(QPen(QColor(255, 255, 255, 80), 6));
    p.drawRoundedRect(tpms_panel, 20, 20);
    p.setPen(QColor(255, 255, 255, 200));
    //p.setRenderHint(QPainter::TextAntialiasing);
    float maxv = 0;
    float minv = 300;
    int font_size = 60;

    if (maxv < s->scene.tpmsPressureFl) {maxv = s->scene.tpmsPressureFl;}
    if (maxv < s->scene.tpmsPressureFr) {maxv = s->scene.tpmsPressureFr;}
    if (maxv < s->scene.tpmsPressureRl) {maxv = s->scene.tpmsPressureRl;}
    if (maxv < s->scene.tpmsPressureRr) {maxv = s->scene.tpmsPressureRr;}
    if (minv > s->scene.tpmsPressureFl) {minv = s->scene.tpmsPressureFl;}
    if (minv > s->scene.tpmsPressureFr) {minv = s->scene.tpmsPressureFr;}
    if (minv > s->scene.tpmsPressureRl) {minv = s->scene.tpmsPressureRl;}
    if (minv > s->scene.tpmsPressureRr) {minv = s->scene.tpmsPressureRr;}

    if (((maxv - minv) > 3 && s->scene.tpmsUnit != 2) || ((maxv - minv) > 0.2 && s->scene.tpmsUnit == 2)) {
      p.setBrush(QColor(255, 0, 0, 150));
    }
    if (s->scene.tpmsUnit != 0) {
      debugText(p, tpms_sp_xr, tpms_sp_yr+15, "TPMS", 150, 33);
      font_size = (s->scene.tpmsUnit == 2) ? 60 : 55;
    } else {
      debugText(p, tpms_sp_xr, tpms_sp_yr+15, "TPMS(psi)", 150, 33);
      font_size = 65;
    }
    if ((s->scene.tpmsPressureFl < 32 && s->scene.tpmsUnit != 2) || (s->scene.tpmsPressureFl < 2.2 && s->scene.tpmsUnit == 2)) {
      p.setPen(yellowColor(200));
      debugText(p, tpms_sp_xr-(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+55, QString::number(s->scene.tpmsPressureFl, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    } else if (s->scene.tpmsPressureFl > 50) {
      p.setPen(whiteColor(200));
      debugText(p, tpms_sp_xr-(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+55, "N/A", 200, font_size);
    } else if ((s->scene.tpmsPressureFl > 45 && s->scene.tpmsUnit != 2) || (s->scene.tpmsPressureFl > 2.8 && s->scene.tpmsUnit == 2)) {
      p.setPen(redColor(200));
      debugText(p, tpms_sp_xr-(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+55, QString::number(s->scene.tpmsPressureFl, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    } else {
      p.setPen(greenColor(200));
      debugText(p, tpms_sp_xr-(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+55, QString::number(s->scene.tpmsPressureFl, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    }
    if ((s->scene.tpmsPressureFr < 32 && s->scene.tpmsUnit != 2) || (s->scene.tpmsPressureFr < 2.2 && s->scene.tpmsUnit == 2)) {
      p.setPen(yellowColor(200));
      debugText(p, tpms_sp_xr+(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+55, QString::number(s->scene.tpmsPressureFr, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    } else if (s->scene.tpmsPressureFr > 50) {
      p.setPen(whiteColor(200));
      debugText(p, tpms_sp_xr+(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+55, "N/A", 200, font_size);
    } else if ((s->scene.tpmsPressureFr > 45 && s->scene.tpmsUnit != 2) || (s->scene.tpmsPressureFr > 2.8 && s->scene.tpmsUnit == 2)) {
      p.setPen(redColor(200));
      debugText(p, tpms_sp_xr+(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+55, QString::number(s->scene.tpmsPressureFr, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    } else {
      p.setPen(greenColor(200));
      debugText(p, tpms_sp_xr+(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+55, QString::number(s->scene.tpmsPressureFr, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    }
    if ((s->scene.tpmsPressureRl < 32 && s->scene.tpmsUnit != 2) || (s->scene.tpmsPressureRl < 2.2 && s->scene.tpmsUnit == 2)) {
      p.setPen(yellowColor(200));
      debugText(p, tpms_sp_xr-(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+95, QString::number(s->scene.tpmsPressureRl, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    } else if (s->scene.tpmsPressureRl > 50) {
      p.setPen(whiteColor(200));
      debugText(p, tpms_sp_xr-(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+95, "N/A", 200, font_size);
    } else if ((s->scene.tpmsPressureRl > 45 && s->scene.tpmsUnit != 2) || (s->scene.tpmsPressureRl > 2.8 && s->scene.tpmsUnit == 2)) {
      p.setPen(redColor(200));
      debugText(p, tpms_sp_xr-(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+95, QString::number(s->scene.tpmsPressureRl, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    } else {
      p.setPen(greenColor(200));
      debugText(p, tpms_sp_xr-(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+95, QString::number(s->scene.tpmsPressureRl, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    }
    if ((s->scene.tpmsPressureRr < 32 && s->scene.tpmsUnit != 2) || (s->scene.tpmsPressureRr < 2.2 && s->scene.tpmsUnit == 2)) {
      p.setPen(yellowColor(200));
      debugText(p, tpms_sp_xr+(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+95, QString::number(s->scene.tpmsPressureRr, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    } else if (s->scene.tpmsPressureRr > 50) {
      p.setPen(whiteColor(200));
      debugText(p, tpms_sp_xr+(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+95, "N/A", 200, font_size);
    } else if ((s->scene.tpmsPressureRr > 45 && s->scene.tpmsUnit != 2) || (s->scene.tpmsPressureRr > 2.8 && s->scene.tpmsUnit == 2)) {
      p.setPen(redColor(200));
      debugText(p, tpms_sp_xr+(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+95, QString::number(s->scene.tpmsPressureRr, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    } else {
      p.setPen(greenColor(200));
      debugText(p, tpms_sp_xr+(s->scene.tpmsUnit != 0?46:50), tpms_sp_yr+95, QString::number(s->scene.tpmsPressureRr, 'f', (s->scene.tpmsUnit != 0?1:0)), 200, font_size);
    }
  }


  if (!comma_stock_ui) {
    // opkr rec
    QRect recbtn_draw(rect().right() - bdr_s - 140 - 20, 905, 140, 140);
    p.setBrush(Qt::NoBrush);
    if (record_stat) p.setBrush(redColor(150));
    p.setPen(QPen(QColor(255, 255, 255, 80), 6));
    p.drawEllipse(recbtn_draw);
    p.setPen(whiteColor(200));
    p.drawText(recbtn_draw, Qt::AlignCenter, QString("REC"));

    // opkr lane selector
    QRect lanebtn_draw(rect().right() - bdr_s - 140 - 20 - 160, 905, 140, 140);
    p.setBrush(Qt::NoBrush);
    if (laneless_stat) p.setBrush(greenColor(150));
    p.setPen(QPen(QColor(255, 255, 255, 80), 6));
    p.drawEllipse(lanebtn_draw);
    p.setPen(whiteColor(200));
    if (lane_stat == 0) {
      configFont(p, "Inter", 39, "SemiBold");
      p.drawText(QRect(rect().right() - bdr_s - 140 - 20 - 160, 890, 140, 140), Qt::AlignCenter, QString("LANE"));
      p.drawText(QRect(rect().right() - bdr_s - 140 - 20 - 160, 920, 140, 140), Qt::AlignCenter, QString("LINE"));
    } else if (lane_stat == 1) {
      configFont(p, "Inter", 39, "SemiBold");
      p.drawText(QRect(rect().right() - bdr_s - 140 - 20 - 160, 890, 140, 140), Qt::AlignCenter, QString("LANE"));
      p.drawText(QRect(rect().right() - bdr_s - 140 - 20 - 160, 920, 140, 140), Qt::AlignCenter, QString("LESS"));
    } else if (lane_stat == 2) {
      p.drawText(lanebtn_draw, Qt::AlignCenter, QString("AUTO"));
    }
  }
  // opkr standstill
  if (standstill && !comma_stock_ui) {
    int minute = 0;
    int second = 0;
    minute = int(ss_elapsed / 60);
    second = int(ss_elapsed) - (minute * 60);
    p.setPen(ochreColor(220));
    debugText(p, mapbox_stat?(rect().right()-bdr_s-295):(rect().right()-bdr_s-545), bdr_s+420, "STOP", 220, mapbox_stat?85:130);
    p.setPen(whiteColor(220));
    debugText(p, mapbox_stat?(rect().right()-bdr_s-295):(rect().right()-bdr_s-545), mapbox_stat?bdr_s+500:bdr_s+550, QString::number(minute).rightJustified(2,'0') + ":" + QString::number(second).rightJustified(2,'0'), 220, mapbox_stat?95:140);
  }

  // opkr autohold
  if (auto_hold && !comma_stock_ui) {
    int y_pos = 0;
    if (s->scene.steer_warning && (s->scene.car_state.getVEgo() < 0.1 || standstill) && s->scene.car_state.getSteeringAngleDeg() < 90) {
      y_pos = 500;
    } else {
      y_pos = 740;
    }
    int width = 500;
    int a_center = s->fb_w/2;
    QRect ah_rect(a_center - width/2, y_pos, width, 145);
    p.setBrush(Qt::NoBrush);
    p.setBrush(blackColor(80));
    p.setPen(QPen(QColor(255, 255, 255, 50), 10));
    p.drawRoundedRect(ah_rect, 20, 20);
    p.setPen(greenColor(150));
    debugText(p, a_center, y_pos + 99, "AUTO HOLD", 150, 79, true);
  }

  // opkr blinker
  if (!comma_stock_ui) {
    float bw = 0;
    float bx = 0;
    float bh = 0;
    if (left_blinker) {
      bw = 250;
      bx = s->fb_w/2 - bw/2;
      bh = 400;
      QPointF leftbsign1[] = {{bx, bh/4}, {bx-bw/4, bh/4}, {bx-bw/2, bh/2}, {bx-bw/4, bh/4+bh/2}, {bx, bh/4+bh/2}, {bx-bw/4, bh/2}};
      bx -= 125;
      QPointF leftbsign2[] = {{bx, bh/4}, {bx-bw/4, bh/4}, {bx-bw/2, bh/2}, {bx-bw/4, bh/4+bh/2}, {bx, bh/4+bh/2}, {bx-bw/4, bh/2}};
      bx -= 125;
      QPointF leftbsign3[] = {{bx, bh/4}, {bx-bw/4, bh/4}, {bx-bw/2, bh/2}, {bx-bw/4, bh/4+bh/2}, {bx, bh/4+bh/2}, {bx-bw/4, bh/2}};

      if (blinker_rate<=120 && blinker_rate>=60) {
        p.setBrush(yellowColor(70));
        p.drawPolygon(leftbsign1, std::size(leftbsign1));
      }
      if (blinker_rate<=100 && blinker_rate>=60) {
        p.setBrush(yellowColor(140));
        p.drawPolygon(leftbsign2, std::size(leftbsign2));
      }
      if (blinker_rate<=80 && blinker_rate>=60) {
        p.setBrush(yellowColor(210));
        p.drawPolygon(leftbsign3, std::size(leftbsign3));
      }
    }
    if (right_blinker) {
      bw = 250;
      bx = s->fb_w/2 - bw/2 + bw;
      bh = 400;
      QPointF rightbsign1[] = {{bx, bh/4}, {bx+bw/4, bh/4}, {bx+bw/2, bh/2}, {bx+bw/4, bh/4+bh/2}, {bx, bh/4+bh/2}, {bx+bw/4, bh/2}};
      bx += 125;
      QPointF rightbsign2[] = {{bx, bh/4}, {bx+bw/4, bh/4}, {bx+bw/2, bh/2}, {bx+bw/4, bh/4+bh/2}, {bx, bh/4+bh/2}, {bx+bw/4, bh/2}};
      bx += 125;
      QPointF rightbsign3[] = {{bx, bh/4}, {bx+bw/4, bh/4}, {bx+bw/2, bh/2}, {bx+bw/4, bh/4+bh/2}, {bx, bh/4+bh/2}, {bx+bw/4, bh/2}};

      if (blinker_rate<=120 && blinker_rate>=60) {
        p.setBrush(yellowColor(70));
        p.drawPolygon(rightbsign1, std::size(rightbsign1));
      }
      if (blinker_rate<=100 && blinker_rate>=60) {
        p.setBrush(yellowColor(140));
        p.drawPolygon(rightbsign2, std::size(rightbsign2));
      }
      if (blinker_rate<=80 && blinker_rate>=60) {
        p.setBrush(yellowColor(210));
        p.drawPolygon(rightbsign3, std::size(rightbsign3));
      }
    }
  }

  // opkr safetysign
  if (!comma_stock_ui) {
    int diameter1 = 185;
    int diameter2 = 170;
    int diameter3 = 202;
    int s_center_x = bdr_s + 305;
    int s_center_y = bdr_s + 100;
    
    int d_center_x = s_center_x;
    int d_center_y = s_center_y + 155;
    int d_width = 220;
    int d_height = 70;
    int opacity = 0;

    QRect rect_s = QRect(s_center_x - diameter1/2, s_center_y - diameter1/2, diameter1, diameter1);
    QRect rect_si = QRect(s_center_x - diameter2/2, s_center_y - diameter2/2, diameter2, diameter2);
    QRect rect_so = QRect(s_center_x - diameter3/2, s_center_y - diameter3/2, diameter3, diameter3);
    QRect rect_d = QRect(d_center_x - d_width/2, d_center_y - d_height/2, d_width, d_height);
    int sl_opacity = 0;
    if (decel_off) {
      sl_opacity = 3;
    } else if (s->scene.pause_spdlimit) {
      sl_opacity = 2;
    } else {
      sl_opacity = 1;
    }

    if (safety_dist != 0) {
      opacity = safety_dist>600 ? 0 : (600 - safety_dist) * 0.425;
      p.setBrush(redColor(opacity/sl_opacity));
      p.setPen(QPen(QColor(255, 255, 255, 100), 7));
      p.drawRoundedRect(rect_d, 8, 8);
      configFont(p, "Inter", 55, "Bold");
      p.setPen(whiteColor(255));
      if (s->scene.is_metric) {
        if (safety_dist >= 1000) {
          p.drawText(rect_d, Qt::AlignCenter, QString::number(safety_dist/1000, 'f', 2) + "km");
        } else {
          p.drawText(rect_d, Qt::AlignCenter, QString::number(safety_dist, 'f', 0) + "m");
        }
      } else {
        if (safety_dist >= 1000) {
          p.drawText(rect_d, Qt::AlignCenter, QString::number(safety_dist/1000, 'f', 2) + "mi");
        } else {
          p.drawText(rect_d, Qt::AlignCenter, QString::number(safety_dist, 'f', 0) + "yd");
        }
      }
    }

    if (safety_speed > 19) {
      if (s->scene.speedlimit_signtype) {
        p.setBrush(whiteColor(255/sl_opacity));
        p.drawRoundedRect(rect_si, 8, 8);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0, 0, 0, 255/sl_opacity), 12));
        p.drawRoundedRect(rect_s, 8, 8);
        p.setPen(QPen(QColor(255, 255, 255, 255/sl_opacity), 10));
        p.drawRoundedRect(rect_so, 8, 8);
        p.setPen(blackColor(255/sl_opacity));
        debugText(p, rect_so.center().x(), rect_so.center().y()-45, "SPEED", 255/sl_opacity, 36, true);
        debugText(p, rect_so.center().x(), rect_so.center().y()-12, "LIMIT", 255/sl_opacity, 36, true);
        debugText(p, rect_so.center().x(), rect_so.center().y()+bdr_s+(safety_speed<100?60:50), QString::number(safety_speed), 255/sl_opacity, safety_speed<100?110:90, true);
      } else {
        p.setBrush(whiteColor(255/sl_opacity));
        p.drawEllipse(rect_si);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(redColor(255/sl_opacity), 20));
        p.drawEllipse(rect_s);
        p.setPen(blackColor(255/sl_opacity));
        debugText(p, rect_si.center().x(), rect_si.center().y()+bdr_s+(safety_speed<100?25:15), QString::number(safety_speed), 255/sl_opacity, safety_speed<100?110:90, true);
      }
    }
  }

  p.restore();
}


// Window that shows camera view and variety of
// info drawn on top

void AnnotatedCameraWidget::drawText(QPainter &p, int x, int y, const QString &text, int alpha) {
  QRect real_rect = getTextRect(p, 0, text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});

  p.setPen(QColor(0xff, 0xff, 0xff, alpha));
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}

void AnnotatedCameraWidget::drawIcon(QPainter &p, int x, int y, QPixmap &img, QBrush bg, float opacity) {
  p.setOpacity(1.0);  // bg dictates opacity of ellipse
  p.setPen(Qt::NoPen);
  p.setBrush(bg);
  p.drawEllipse(x - btn_size / 2, y - btn_size / 2, btn_size, btn_size);
  p.setOpacity(opacity);
  p.drawPixmap(x - img.size().width() / 2, y - img.size().height() / 2, img);
  p.setOpacity(1.0);
}


void AnnotatedCameraWidget::initializeGL() {
  CameraWidget::initializeGL();
  qInfo() << "OpenGL version:" << QString((const char*)glGetString(GL_VERSION));
  qInfo() << "OpenGL vendor:" << QString((const char*)glGetString(GL_VENDOR));
  qInfo() << "OpenGL renderer:" << QString((const char*)glGetString(GL_RENDERER));
  qInfo() << "OpenGL language version:" << QString((const char*)glGetString(GL_SHADING_LANGUAGE_VERSION));

  prev_draw_t = millis_since_boot();
  setBackgroundColor(bg_colors[STATUS_DISENGAGED]);
}

void AnnotatedCameraWidget::updateFrameMat() {
  CameraWidget::updateFrameMat();
  UIState *s = uiState();
  int w = width(), h = height();

  s->fb_w = w;
  s->fb_h = h;

  // Apply transformation such that video pixel coordinates match video
  // 1) Put (0, 0) in the middle of the video
  // 2) Apply same scaling as video
  // 3) Put (0, 0) in top left corner of video
  s->car_space_transform.reset();
  s->car_space_transform.translate(w / 2 - x_offset, h / 2 - y_offset)
      .scale(zoom, zoom)
      .translate(-intrinsic_matrix.v[2], -intrinsic_matrix.v[5]);
}

void AnnotatedCameraWidget::drawLaneLines(QPainter &painter, const UIState *s) {
  painter.save();

  const UIScene &scene = s->scene;
  SubMaster &sm = *(s->sm);

  // lanelines
  for (int i = 0; i < std::size(scene.lane_line_vertices); ++i) {
    painter.setBrush(QColor::fromRgbF(1.0, 1.0, 1.0, std::clamp<float>(scene.lane_line_probs[i], 0.0, 0.7)));
    painter.drawPolygon(scene.lane_line_vertices[i]);
  }

  // road edges
  for (int i = 0; i < std::size(scene.road_edge_vertices); ++i) {
    painter.setBrush(QColor::fromRgbF(1.0, 0, 0, std::clamp<float>(1.0 - scene.road_edge_stds[i], 0.0, 1.0)));
    painter.drawPolygon(scene.road_edge_vertices[i]);
  }

  // paint path
  QLinearGradient bg(0, height(), 0, 0);
  if (sm["controlsState"].getControlsState().getExperimentalMode()) {
    // The first half of track_vertices are the points for the right side of the path
    // and the indices match the positions of accel from uiPlan
    const auto &acceleration = sm["uiPlan"].getUiPlan().getAccel();
    const int max_len = std::min<int>(scene.track_vertices.length() / 2, acceleration.size());

    for (int i = 0; i < max_len; ++i) {
      // Some points are out of frame
      if (scene.track_vertices[i].y() < 0 || scene.track_vertices[i].y() > height()) continue;

      // Flip so 0 is bottom of frame
      float lin_grad_point = (height() - scene.track_vertices[i].y()) / height();

      // speed up: 120, slow down: 0
      float path_hue = fmax(fmin(60 + acceleration[i] * 35, 120), 0);
      // FIXME: painter.drawPolygon can be slow if hue is not rounded
      path_hue = int(path_hue * 100 + 0.5) / 100;

      float saturation = fmin(fabs(acceleration[i] * 1.5), 1);
      float lightness = util::map_val(saturation, 0.0f, 1.0f, 0.95f, 0.62f);  // lighter when grey
      float alpha = util::map_val(lin_grad_point, 0.75f / 2.f, 0.75f, 0.4f, 0.0f);  // matches previous alpha fade
      bg.setColorAt(lin_grad_point, QColor::fromHslF(path_hue / 360., saturation, lightness, alpha));

      // Skip a point, unless next is last
      i += (i + 2) < max_len ? 1 : 0;
    }

  } else {
    bg.setColorAt(0.0, QColor::fromHslF(148 / 360., 0.94, 0.51, 0.4));
    bg.setColorAt(0.5, QColor::fromHslF(112 / 360., 1.0, 0.68, 0.35));
    bg.setColorAt(1.0, QColor::fromHslF(112 / 360., 1.0, 0.68, 0.0));
  }

  painter.setBrush(bg);
  painter.drawPolygon(scene.track_vertices);

  painter.restore();
}

void AnnotatedCameraWidget::drawDriverState(QPainter &painter, const UIState *s) {
  const UIScene &scene = s->scene;

  painter.save();

  // base icon
  int x = rightHandDM ? rect().right() -  (btn_size - 24) / 2 - (bdr_s * 2) : (btn_size - 24) / 2 + (bdr_s * 2);
  int y = rect().bottom() - footer_h / 2;
  float opacity = dmActive ? 0.65 : 0.2;
  drawIcon(painter, x, y, dm_img, blackColor(70), opacity);

  // face
  QPointF face_kpts_draw[std::size(default_face_kpts_3d)];
  float kp;
  for (int i = 0; i < std::size(default_face_kpts_3d); ++i) {
    kp = (scene.face_kpts_draw[i].v[2] - 8) / 120 + 1.0;
    face_kpts_draw[i] = QPointF(scene.face_kpts_draw[i].v[0] * kp + x, scene.face_kpts_draw[i].v[1] * kp + y);
  }

  painter.setPen(QPen(QColor::fromRgbF(1.0, 1.0, 1.0, opacity), 5.2, Qt::SolidLine, Qt::RoundCap));
  painter.drawPolyline(face_kpts_draw, std::size(default_face_kpts_3d));

  // tracking arcs
  const int arc_l = 133;
  const float arc_t_default = 6.7;
  const float arc_t_extend = 12.0;
  QColor arc_color = QColor::fromRgbF(0.545 - 0.445 * s->engaged(),
                                      0.545 + 0.4 * s->engaged(),
                                      0.545 - 0.285 * s->engaged(),
                                      0.4 * (1.0 - dm_fade_state));
  float delta_x = -scene.driver_pose_sins[1] * arc_l / 2;
  float delta_y = -scene.driver_pose_sins[0] * arc_l / 2;
  painter.setPen(QPen(arc_color, arc_t_default+arc_t_extend*fmin(1.0, scene.driver_pose_diff[1] * 5.0), Qt::SolidLine, Qt::RoundCap));
  painter.drawArc(QRectF(std::fmin(x + delta_x, x), y - arc_l / 2, fabs(delta_x), arc_l), (scene.driver_pose_sins[1]>0 ? 90 : -90) * 16, 180 * 16);
  painter.setPen(QPen(arc_color, arc_t_default+arc_t_extend*fmin(1.0, scene.driver_pose_diff[0] * 5.0), Qt::SolidLine, Qt::RoundCap));
  painter.drawArc(QRectF(x - arc_l / 2, std::fmin(y + delta_y, y), arc_l, fabs(delta_y)), (scene.driver_pose_sins[0]>0 ? 0 : 180) * 16, 180 * 16);

  painter.restore();
}

void AnnotatedCameraWidget::drawLead(QPainter &painter, const cereal::RadarState::LeadData::Reader &lead_data, const QPointF &vd) {
  painter.save();

  const float speedBuff = 10.;
  const float leadBuff = 40.;
  const float d_rel = lead_data.getDRel();
  const float v_rel = lead_data.getVRel();

  float fillAlpha = 0;
  if (d_rel < leadBuff) {
    fillAlpha = 255 * (1.0 - (d_rel / leadBuff));
    if (v_rel < 0) {
      fillAlpha += 255 * (-1 * (v_rel / speedBuff));
    }
    fillAlpha = (int)(fmin(fillAlpha, 255));
  }

  float sz = std::clamp((25 * 30) / (d_rel / 3 + 30), 15.0f, 30.0f) * 2.35;
  float x = std::clamp((float)vd.x(), 0.f, width() - sz / 2);
  float y = std::fmin(height() - sz * .6, (float)vd.y());

  float g_xo = sz / 5;
  float g_yo = sz / 10;

  UIState *s = uiState();

  // opkr
  if (s->scene.radarDistance < 149) {
    QPointF glow[] = {{x + (sz * 1.35) + g_xo, y + sz + g_yo}, {x, y - g_xo}, {x - (sz * 1.35) - g_xo, y + sz + g_yo}};
    painter.setBrush(QColor(218, 202, 37, 255));
    painter.drawPolygon(glow, std::size(glow));

    // chevron
    QPointF chevron[] = {{x + (sz * 1.25), y + sz}, {x, y}, {x - (sz * 1.25), y + sz}};
    painter.setBrush(redColor(fillAlpha));
    painter.drawPolygon(chevron, std::size(chevron));
    painter.setPen(QColor(0x0, 0x0, 0xff));
    //painter.setRenderHint(QPainter::TextAntialiasing);
    configFont(painter, "Inter", 35, "SemiBold");
    painter.drawText(QRect(x - (sz * 1.25), y, 2 * (sz * 1.25), sz * 1.25), Qt::AlignCenter, QString("R"));
  } else {
    QPointF glow[] = {{x + (sz * 1.35) + g_xo, y + sz + g_yo}, {x, y - g_xo}, {x - (sz * 1.35) - g_xo, y + sz + g_yo}};
    painter.setBrush(QColor(0, 255, 0, 255));
    painter.drawPolygon(glow, std::size(glow));

    // chevron
    QPointF chevron[] = {{x + (sz * 1.25), y + sz}, {x, y}, {x - (sz * 1.25), y + sz}};
    painter.setBrush(greenColor(fillAlpha));
    painter.drawPolygon(chevron, std::size(chevron));
    painter.setPen(QColor(0xff, 0xff, 0xff));
    //painter.setRenderHint(QPainter::TextAntialiasing);
    configFont(painter, "Inter", 35, "SemiBold");
    painter.drawText(QRect(x - (sz * 1.25), y, 2 * (sz * 1.25), sz * 1.25), Qt::AlignCenter, QString("V"));
  }

  painter.restore();
}

void AnnotatedCameraWidget::paintGL() {
  UIState *s = uiState();
  SubMaster &sm = *(s->sm);
  const double start_draw_t = millis_since_boot();
  const cereal::ModelDataV2::Reader &model = sm["modelV2"].getModelV2();
  const cereal::RadarState::Reader &radar_state = sm["radarState"].getRadarState();

  // draw camera frame
  {
    std::lock_guard lk(frame_lock);

    if (frames.empty()) {
      if (skip_frame_count > 0) {
        skip_frame_count--;
        qDebug() << "skipping frame, not ready";
        return;
      }
    } else {
      // skip drawing up to this many frames if we're
      // missing camera frames. this smooths out the
      // transitions from the narrow and wide cameras
      skip_frame_count = 5;
    }

    // Wide or narrow cam dependent on speed
    bool has_wide_cam = available_streams.count(VISION_STREAM_WIDE_ROAD);
    if (has_wide_cam) {
      float v_ego = sm["carState"].getCarState().getVEgo();
      if ((v_ego < 10) || available_streams.size() == 1) {
        wide_cam_requested = true;
      } else if (v_ego > 15) {
        wide_cam_requested = false;
      }
      wide_cam_requested = wide_cam_requested && sm["controlsState"].getControlsState().getExperimentalMode();
      // for replay of old routes, never go to widecam
      wide_cam_requested = wide_cam_requested && s->scene.calibration_wide_valid;
    }
    CameraWidget::setStreamType(wide_cam_requested ? VISION_STREAM_WIDE_ROAD : VISION_STREAM_ROAD);

    s->scene.wide_cam = CameraWidget::getStreamType() == VISION_STREAM_WIDE_ROAD;
    if (s->scene.calibration_valid) {
      auto calib = s->scene.wide_cam ? s->scene.view_from_wide_calib : s->scene.view_from_calib;
      CameraWidget::updateCalibration(calib);
    } else {
      CameraWidget::updateCalibration(DEFAULT_CALIBRATION);
    }
    CameraWidget::setFrameId(model.getFrameId());
    CameraWidget::paintGL();
  }

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);

  if (s->worldObjectsVisible()) {
    if (sm.rcv_frame("modelV2") > s->scene.started_frame) {
      update_model(s, sm["modelV2"].getModelV2(), sm["uiPlan"].getUiPlan());
      if (sm.rcv_frame("radarState") > s->scene.started_frame) {
        update_leads(s, radar_state, sm["modelV2"].getModelV2().getPosition());
      }
    }

    drawLaneLines(painter, s);

    if (true) {
      auto lead_one = radar_state.getLeadOne();
      auto lead_two = radar_state.getLeadTwo();
      if (lead_one.getStatus()) {
        drawLead(painter, lead_one, s->scene.lead_vertices[0]);
      }
      if (lead_two.getStatus() && (std::abs(lead_one.getDRel() - lead_two.getDRel()) > 3.0)) {
        drawLead(painter, lead_two, s->scene.lead_vertices[1]);
      }
    }
  }

  // DMoji
  if (!hideDM && (sm.rcv_frame("driverStateV2") > s->scene.started_frame)) {
    update_dmonitoring(s, sm["driverStateV2"].getDriverStateV2(), dm_fade_state, rightHandDM);
    drawDriverState(painter, s);
  }

  drawHud(painter);

  double cur_draw_t = millis_since_boot();
  double dt = cur_draw_t - prev_draw_t;
  double fps = fps_filter.update(1. / dt * 1000);
  if (fps < 15) {
    LOGW("slow frame rate: %.2f fps", fps);
  }
  prev_draw_t = cur_draw_t;

  // publish debug msg
  MessageBuilder msg;
  auto m = msg.initEvent().initUiDebug();
  m.setDrawTimeMillis(cur_draw_t - start_draw_t);
  pm->send("uiDebug", msg);
}

void AnnotatedCameraWidget::showEvent(QShowEvent *event) {
  CameraWidget::showEvent(event);

  ui_update_params(uiState());
  prev_draw_t = millis_since_boot();
}

void AnnotatedCameraWidget::debugText(QPainter &p, int x, int y, const QString &text, int alpha, int fontsize, bool bold) {
  if (bold) {
    configFont(p, "Inter", fontsize, "Bold");
  } else {
    configFont(p, "Inter", fontsize, "SemiBold");
  }
  configFont(p, "Inter", fontsize, "SemiBold");
  QFontMetrics fm(p.font());
  QRect init_rect = fm.boundingRect(text);
  QRect real_rect = fm.boundingRect(init_rect, 0, text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});

  //p.setPen(QColor(0xff, 0xff, 0xff, alpha));
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}

void AnnotatedCameraWidget::uiText(QPainter &p, int x, int y, const QString &text, int alpha) {
  QFontMetrics fm(p.font());
  QRect init_rect = fm.boundingRect(text);
  QRect real_rect = fm.boundingRect(init_rect, 0, text);
  real_rect.moveCenter({x + real_rect.width() / 2, y - real_rect.height() / 2});

  p.setPen(QColor(0xff, 0xff, 0xff, alpha));
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}