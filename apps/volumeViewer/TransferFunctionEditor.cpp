// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include "TransferFunctionEditor.h"

TransferFunctionEditor::TransferFunctionEditor(OSPTransferFunction transferFunction)
  : transferFunction(transferFunction) 
{

  //! Make sure we have an existing transfer function.
  if(!transferFunction)
    throw std::runtime_error("must be constructed with an existing transfer function");

  //! Load color maps.
  loadColorMaps();

  //! Setup UI elements.
  QVBoxLayout * layout = new QVBoxLayout();
  layout->setSizeConstraint(QLayout::SetMinimumSize);
  setLayout(layout);

  //! Save and load buttons.
  QWidget * saveLoadWidget = new QWidget();
  QHBoxLayout * hboxLayout = new QHBoxLayout();
  saveLoadWidget->setLayout(hboxLayout);

  QPushButton * saveButton = new QPushButton("Save");
  connect(saveButton, SIGNAL(clicked()), this, SLOT(save()));
  hboxLayout->addWidget(saveButton);

  QPushButton * loadButton = new QPushButton("Load");
  connect(loadButton, SIGNAL(clicked()), this, SLOT(load()));
  hboxLayout->addWidget(loadButton);

  layout->addWidget(saveLoadWidget);

  //! Form layout.
  QWidget * formWidget = new QWidget();
  QFormLayout * formLayout = new QFormLayout();
  formWidget->setLayout(formLayout);
  QMargins margins = formLayout->contentsMargins();
  margins.setTop(0); margins.setBottom(0);
  formLayout->setContentsMargins(margins);
  layout->addWidget(formWidget);

  //! Color map choice.
  for(unsigned int i=0; i<colorMaps.size(); i++)
    colorMapComboBox.addItem(colorMaps[i].getName().c_str());

  connect(&colorMapComboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(setColorMapIndex(int)));

  formLayout->addRow("Color map", &colorMapComboBox);

  //! Data value range, used as the domain for both color and opacity components of the transfer function.
  dataValueMinSpinBox.setRange(-999999., 999999.);
  dataValueMaxSpinBox.setRange(-999999., 999999.);
  dataValueScaleSpinBox.setRange(-100, 100);
  dataValueMinSpinBox.setDecimals(6);
  dataValueMaxSpinBox.setDecimals(6);

  connect(&dataValueMinSpinBox, SIGNAL(valueChanged(double)), this, SLOT(updateDataValueRange()));
  connect(&dataValueMaxSpinBox, SIGNAL(valueChanged(double)), this, SLOT(updateDataValueRange()));
  connect(&dataValueScaleSpinBox, SIGNAL(valueChanged(int)), this, SLOT(updateDataValueRange()));

  formLayout->addRow("Data value min", &dataValueMinSpinBox);
  formLayout->addRow("Data value max", &dataValueMaxSpinBox);
  formLayout->addRow("Data value scale (10^X)", &dataValueScaleSpinBox);

  //! Widget containing all opacity-related widgets.
  QWidget * opacityGroup = new QWidget();
  QGridLayout * gridLayout = new QGridLayout();
  opacityGroup->setLayout(gridLayout);

  //! Vertical axis label.
  QLabel * verticalAxisLabel = new QLabel("O\np\na\nc\ni\nt\ny");
  verticalAxisLabel->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
  gridLayout->addWidget(verticalAxisLabel, 0, 0);

  //! Opacity values widget.
  connect(&opacityValuesWidget, SIGNAL(updated()), this, SLOT(updateOpacityValues()));
  gridLayout->addWidget(&opacityValuesWidget, 0, 1);

  //! Opacity scaling slider, defaults to median value in range.
  opacityScalingSlider.setValue(int(0.5f * (opacityScalingSlider.minimum() + opacityScalingSlider.maximum())));
  opacityScalingSlider.setOrientation(Qt::Vertical);

  connect(&opacityScalingSlider, SIGNAL(valueChanged(int)), this, SLOT(updateOpacityValues()));

  gridLayout->addWidget(&opacityScalingSlider, 0, 2);

  //! Horizontal axis label.
  QLabel * horizontalAxisLabel = new QLabel("Data value");
  horizontalAxisLabel->setAlignment(Qt::AlignHCenter);
  gridLayout->addWidget(horizontalAxisLabel, 1, 1);

  layout->addWidget(opacityGroup);

  //! Set defaults.
  setColorMapIndex(0);
  setDataValueRange(osp::vec2f(0.0f, 1.0f));
  updateOpacityValues();
}

void TransferFunctionEditor::load(std::string filename) {

  //! Get filename if not specified.
  if(filename.empty())
    filename = QFileDialog::getOpenFileName(this, tr("Load transfer function"), ".", "Transfer function files (*.tfn)").toStdString();

  if(filename.empty())
    return;

  //! Get serialized transfer function state from file.
  QFile file(filename.c_str());
  bool success = file.open(QIODevice::ReadOnly);

  if(!success) {
    std::cerr << "unable to open " << filename << std::endl;
    return;
  }

  QDataStream in(&file);

  int colorMapIndex;
  in >> colorMapIndex;

  double dataValueMin, dataValueMax;
  in >> dataValueMin >> dataValueMax;

  QVector<QPointF> points;
  in >> points;

  int opacityScalingIndex;
  in >> opacityScalingIndex;

  //! Update transfer function state. Update values of the UI elements directly to signal appropriate slots.
  colorMapComboBox.setCurrentIndex(colorMapIndex);
  setDataValueRange(osp::vec2f(dataValueMin, dataValueMax));
  opacityValuesWidget.setPoints(points);
  opacityScalingSlider.setValue(opacityScalingIndex);
}

void TransferFunctionEditor::setDataValueRange(osp::vec2f dataValueRange) {

  //! Determine appropriate scaling exponent (base 10) for the data value range in the widget.
  int scaleExponent = round(log10f(0.5f * (dataValueRange.y - dataValueRange.x)));

  //! Don't use a scaling exponent <= 5.
  if(abs(scaleExponent) <= 5)
    scaleExponent = 0;

  //! Set widget values.
  dataValueMinSpinBox.setValue(dataValueRange.x / powf(10.f, scaleExponent));
  dataValueMaxSpinBox.setValue(dataValueRange.y / powf(10.f, scaleExponent));
  dataValueScaleSpinBox.setValue(scaleExponent);
}

void TransferFunctionEditor::updateOpacityValues() {

  //! Default to 256 discretizations of the opacities over the domain.
  std::vector<float> opacityValues = opacityValuesWidget.getInterpolatedValuesOverInterval(256);

  //! Opacity scaling factor (normalized in [0, 1]).
  const float opacityScalingNormalized = float(opacityScalingSlider.value() - opacityScalingSlider.minimum()) / float(opacityScalingSlider.maximum() - opacityScalingSlider.minimum());

  //! Scale opacity values.
  for (unsigned int i=0; i < opacityValues.size(); i++)
    opacityValues[i] *= opacityScalingNormalized;

  //! Update OSPRay transfer function.
  OSPData opacityValuesData = ospNewData(opacityValues.size(), OSP_FLOAT, opacityValues.data());
  ospSetData(transferFunction, "opacities", opacityValuesData);

  //! Commit and emit signal.
  ospCommit(transferFunction);
  emit committed();
}

void TransferFunctionEditor::save() {

  //! Get filename.
  QString filename = QFileDialog::getSaveFileName(this, "Save transfer function", ".", "Transfer function files (*.tfn)");

  if(filename.isNull())
    return;

  //! Make sure the filename has the proper extension.
  if(filename.endsWith(".tfn") != true)
    filename += ".tfn";

  //! Serialize transfer function state to file.
  QFile file(filename);
  bool success = file.open(QIODevice::WriteOnly);

  if(!success) {
    std::cerr << "unable to open " << filename.toStdString() << std::endl;
    return;
  }

  QDataStream out(&file);

  out << colorMapComboBox.currentIndex();
  out << dataValueMinSpinBox.value();
  out << dataValueMaxSpinBox.value();
  out << opacityValuesWidget.getPoints();
  out << opacityScalingSlider.value();
}

void TransferFunctionEditor::setColorMapIndex(int index) {

  //! Set transfer function color properties for this color map.
  std::vector<osp::vec3f> colors = colorMaps[index].getColors();

  OSPData colorsData = ospNewData(colors.size(), OSP_FLOAT3, colors.data());
  ospSetData(transferFunction, "colors", colorsData);

  //! Set transfer function widget background image.
  opacityValuesWidget.setBackgroundImage(colorMaps[index].getImage());

  //! Commit and emit signal.
  ospCommit(transferFunction);
  emit committed();
}

void TransferFunctionEditor::updateDataValueRange() {

  //! Data value scale.
  float dataValueScale = powf(10.f, float(dataValueScaleSpinBox.value()));

  //! Set the minimum and maximum values in the domain for both color and opacity components of the transfer function.
  ospSet2f(transferFunction, "valueRange", dataValueScale * float(dataValueMinSpinBox.value()), dataValueScale * float(dataValueMaxSpinBox.value()));

  //! Commit and emit signal.
  ospCommit(transferFunction);
  emit committed();
}

void TransferFunctionEditor::loadColorMaps() {

  //! Color maps based on ParaView default color maps.

  std::vector<osp::vec3f> colors;

  //! Jet.
  colors.clear();
  colors.push_back(osp::vec3f(0         , 0           , 0.562493   ));
  colors.push_back(osp::vec3f(0         , 0           , 1          ));
  colors.push_back(osp::vec3f(0         , 1           , 1          ));
  colors.push_back(osp::vec3f(0.500008  , 1           , 0.500008   ));
  colors.push_back(osp::vec3f(1         , 1           , 0          ));
  colors.push_back(osp::vec3f(1         , 0           , 0          ));
  colors.push_back(osp::vec3f(0.500008  , 0           , 0          ));
  colorMaps.push_back(ColorMap("Jet", colors));

  //! Ice / fire.
  colors.clear();
  colors.push_back(osp::vec3f(0         , 0           , 0           ));
  colors.push_back(osp::vec3f(0         , 0.120394    , 0.302678    ));
  colors.push_back(osp::vec3f(0         , 0.216587    , 0.524575    ));
  colors.push_back(osp::vec3f(0.0552529 , 0.345022    , 0.659495    ));
  colors.push_back(osp::vec3f(0.128054  , 0.492592    , 0.720287    ));
  colors.push_back(osp::vec3f(0.188952  , 0.641306    , 0.792096    ));
  colors.push_back(osp::vec3f(0.327672  , 0.784939    , 0.873426    ));
  colors.push_back(osp::vec3f(0.60824   , 0.892164    , 0.935546    ));
  colors.push_back(osp::vec3f(0.881376  , 0.912184    , 0.818097    ));
  colors.push_back(osp::vec3f(0.9514    , 0.835615    , 0.449271    ));
  colors.push_back(osp::vec3f(0.904479  , 0.690486    , 0           ));
  colors.push_back(osp::vec3f(0.854063  , 0.510857    , 0           ));
  colors.push_back(osp::vec3f(0.777096  , 0.330175    , 0.000885023 ));
  colors.push_back(osp::vec3f(0.672862  , 0.139086    , 0.00270085  ));
  colors.push_back(osp::vec3f(0.508812  , 0           , 0           ));
  colors.push_back(osp::vec3f(0.299413  , 0.000366217 , 0.000549325 ));
  colors.push_back(osp::vec3f(0.0157473 , 0.00332647  , 0           ));
  colorMaps.push_back(ColorMap("Ice / Fire", colors));

  //! Cool to warm.
  colors.clear();
  colors.push_back(osp::vec3f(0.231373  , 0.298039    , 0.752941    ));
  colors.push_back(osp::vec3f(0.865003  , 0.865003    , 0.865003    ));
  colors.push_back(osp::vec3f(0.705882  , 0.0156863   , 0.14902     ));
  colorMaps.push_back(ColorMap("Cool to Warm", colors));

  //! Blue to red rainbow.
  colors.clear();
  colors.push_back(osp::vec3f(0         , 0           , 1           ));
  colors.push_back(osp::vec3f(1         , 0           , 0           ));
  colorMaps.push_back(ColorMap("Blue to Red Rainbow", colors));

  //! Grayscale.
  colors.clear();
  colors.push_back(osp::vec3f(0.));
  colors.push_back(osp::vec3f(1.));
  colorMaps.push_back(ColorMap("Grayscale", colors));
}
