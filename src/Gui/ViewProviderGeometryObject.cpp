/***************************************************************************
 *   Copyright (c) 2006 Werner Mayer <wmayer[at]users.sourceforge.net>     *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"

#ifndef _PreComp_
#include <Inventor/SoPickedPoint.h>
#include <Inventor/actions/SoRayPickAction.h>
#include <Inventor/actions/SoSearchAction.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoCamera.h>
#include <Inventor/nodes/SoDirectionalLight.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoFont.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#endif

#include <Inventor/nodes/SoResetTransform.h>

#include <App/GeoFeature.h>
#include <App/PropertyGeo.h>

#include "Application.h"
#include "Document.h"
#include "SoFCBoundingBox.h"
#include "SoFCSelection.h"
#include "View3DInventorViewer.h"
#include "ViewProviderGeometryObject.h"
#include "ViewProviderGeometryObjectPy.h"

using namespace Gui;

PROPERTY_SOURCE(Gui::ViewProviderGeometryObject, Gui::ViewProviderDragger)

const App::PropertyIntegerConstraint::Constraints intPercent = {0, 100, 5};

ViewProviderGeometryObject::ViewProviderGeometryObject()
{
    ParameterGrp::handle hGrp =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");

    int initialTransparency = hGrp->GetInt("DefaultShapeTransparency", 0);

    static const char* dogroup = "Display Options";
    static const char* sgroup = "Selection";
    static const char* osgroup = "Object Style";

    ADD_PROPERTY_TYPE(Transparency,
                      (initialTransparency),
                      osgroup,
                      App::Prop_None,
                      "Set object transparency");
    Transparency.setConstraints(&intPercent);

    App::Material mat(App::Material::DEFAULT);
    auto geometry = dynamic_cast<App::GeoFeature*>(getObject());
    if (geometry) {
        mat = geometry->getMaterialAppearance();
    } else {
        // This is handled in the material code when using the object appearance
        bool randomColor = hGrp->GetBool("RandomColor", false);
        float r, g, b;

        if (randomColor) {
            auto fMax = (float)RAND_MAX;
            r = (float)rand() / fMax;
            g = (float)rand() / fMax;
            b = (float)rand() / fMax;
        }
        else {
            unsigned long shcol = hGrp->GetUnsigned("DefaultShapeColor", 3435980543UL);
            r = ((shcol >> 24) & 0xff) / 255.0;
            g = ((shcol >> 16) & 0xff) / 255.0;
            b = ((shcol >> 8) & 0xff) / 255.0;
        }
        mat.diffuseColor = App::Color(r,g,b);
    }

    ADD_PROPERTY_TYPE(ShapeAppearance, (mat), osgroup, App::Prop_None, "Shape appearrance");
    ADD_PROPERTY_TYPE(BoundingBox, (false), dogroup, App::Prop_None, "Display object bounding box");
    ADD_PROPERTY_TYPE(Selectable,
                      (true),
                      sgroup,
                      App::Prop_None,
                      "Set if the object is selectable in the 3d view");

    bool enableSel = hGrp->GetBool("EnableSelection", true);
    Selectable.setValue(enableSel);

    pcShapeMaterial = new SoMaterial;
    setSoMaterial(mat);
    pcShapeMaterial->ref();

    pcBoundingBox = new Gui::SoFCBoundingBox;
    pcBoundingBox->ref();

    pcBoundColor = new SoBaseColor();
    pcBoundColor->ref();

    sPixmap = "Feature";
}

ViewProviderGeometryObject::~ViewProviderGeometryObject()
{
    pcShapeMaterial->unref();
    pcBoundingBox->unref();
    pcBoundColor->unref();
}

void ViewProviderGeometryObject::onChanged(const App::Property* prop)
{
    // Actually, the properties 'ShapeColor' and 'Transparency' are part of the property
    // 'ShapeMaterial'. Both redundant properties are kept due to more convenience for the user. But
    // we must keep the values consistent of all these properties.
    std::string propName = prop->getName();
    if (prop == &Selectable) {
        bool Sel = Selectable.getValue();
        setSelectable(Sel);
    }
    else if (prop == &Transparency) {
        long value = (long)(100 * ShapeAppearance.getTransparency());
        if (value != Transparency.getValue()) {
            float trans = (float)Transparency.getValue() / 100.0f;
            pcShapeMaterial->transparency = trans;
            ShapeAppearance.setTransparency(trans);
        }
    }
    else if (prop == &ShapeAppearance) {
        if (getObject() && getObject()->testStatus(App::ObjectStatus::TouchOnColorChange)) {
            getObject()->touch(true);
        }
        const App::Material& Mat = ShapeAppearance[0];
        long value = (long)(100.0 * ShapeAppearance.getTransparency() + 0.5);
        if (value != Transparency.getValue()) {
            Transparency.setValue(value);
        }
        setSoMaterial(Mat);
    }
    else if (prop == &BoundingBox) {
        showBoundingBox(BoundingBox.getValue());
    }

    ViewProviderDragger::onChanged(prop);
}

void ViewProviderGeometryObject::attach(App::DocumentObject* pcObj)
{
    ViewProviderDragger::attach(pcObj);
}

void ViewProviderGeometryObject::updateData(const App::Property* prop)
{
    std::string propName = prop->getName();
    if (propName == "Shape") {
        // Reapply the appearance
        const App::Material& Mat = ShapeAppearance[0];
        setSoMaterial(Mat);
    }
    if (prop->isDerivedFrom(App::PropertyComplexGeoData::getClassTypeId())) {
        Base::BoundBox3d box =
            static_cast<const App::PropertyComplexGeoData*>(prop)->getBoundingBox();
        pcBoundingBox->minBounds.setValue(box.MinX, box.MinY, box.MinZ);
        pcBoundingBox->maxBounds.setValue(box.MaxX, box.MaxY, box.MaxZ);
    }
    else if (prop->isDerivedFrom(App::PropertyPlacement::getClassTypeId())) {
        auto geometry = dynamic_cast<App::GeoFeature*>(getObject());
        if (geometry && prop == &geometry->Placement) {
            const App::PropertyComplexGeoData* data = geometry->getPropertyOfGeometry();
            if (data) {
                Base::BoundBox3d box = data->getBoundingBox();
                pcBoundingBox->minBounds.setValue(box.MinX, box.MinY, box.MinZ);
                pcBoundingBox->maxBounds.setValue(box.MaxX, box.MaxY, box.MaxZ);
            }
        }
    }
    else if (std::string(prop->getName()) == "ShapeMaterial") {
        // Set the appearance from the material
        auto geometry = dynamic_cast<App::GeoFeature*>(getObject());
        if (geometry) {
            auto material = geometry->getMaterialAppearance();
            ShapeAppearance.setValue(material);
        }
    }

    ViewProviderDragger::updateData(prop);
}

SoPickedPointList ViewProviderGeometryObject::getPickedPoints(const SbVec2s& pos,
                                                              const View3DInventorViewer& viewer,
                                                              bool pickAll) const
{
    auto root = new SoSeparator;
    root->ref();
    root->addChild(viewer.getHeadlight());
    root->addChild(viewer.getSoRenderManager()->getCamera());
    root->addChild(getRoot());

    SoRayPickAction rp(viewer.getSoRenderManager()->getViewportRegion());
    rp.setPickAll(pickAll);
    rp.setRadius(viewer.getPickRadius());
    rp.setPoint(pos);
    rp.apply(root);
    root->unref();

    // returns a copy of the list
    return rp.getPickedPointList();
}

SoPickedPoint* ViewProviderGeometryObject::getPickedPoint(const SbVec2s& pos,
                                                          const View3DInventorViewer& viewer) const
{
    auto root = new SoSeparator;
    root->ref();
    root->addChild(viewer.getHeadlight());
    root->addChild(viewer.getSoRenderManager()->getCamera());
    root->addChild(getRoot());

    SoRayPickAction rp(viewer.getSoRenderManager()->getViewportRegion());
    rp.setPoint(pos);
    rp.setRadius(viewer.getPickRadius());
    rp.apply(root);
    root->unref();

    // returns a copy of the point
    SoPickedPoint* pick = rp.getPickedPoint();
    // return (pick ? pick->copy() : 0); // needs the same instance of CRT under MS Windows
    return (pick ? new SoPickedPoint(*pick) : nullptr);
}

unsigned long ViewProviderGeometryObject::getBoundColor() const
{
    ParameterGrp::handle hGrp =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    unsigned long bbcol =
        hGrp->GetUnsigned("BoundingBoxColor", 4294967295UL);  // white (255,255,255)
    return bbcol;
}

void ViewProviderGeometryObject::setSoMaterial(const App::Material& source)
{
    pcShapeMaterial->ambientColor.setValue(source.ambientColor.r,
                                           source.ambientColor.g,
                                           source.ambientColor.b);
    pcShapeMaterial->diffuseColor.setValue(source.diffuseColor.r,
                                           source.diffuseColor.g,
                                           source.diffuseColor.b);
    pcShapeMaterial->specularColor.setValue(source.specularColor.r,
                                            source.specularColor.g,
                                            source.specularColor.b);
    pcShapeMaterial->emissiveColor.setValue(source.emissiveColor.r,
                                            source.emissiveColor.g,
                                            source.emissiveColor.b);
    pcShapeMaterial->shininess.setValue(source.shininess);
    pcShapeMaterial->transparency.setValue(source.transparency);
}

namespace
{
float getBoundBoxFontSize()
{
    ParameterGrp::handle hGrp =
        App::GetApplication().GetParameterGroupByPath("User parameter:BaseApp/Preferences/View");
    return hGrp->GetFloat("BoundingBoxFontSize", 10.0);
}
}  // namespace

void ViewProviderGeometryObject::showBoundingBox(bool show)
{
    if (!pcBoundSwitch && show) {
        unsigned long bbcol = getBoundColor();
        float r, g, b;
        r = ((bbcol >> 24) & 0xff) / 255.0;
        g = ((bbcol >> 16) & 0xff) / 255.0;
        b = ((bbcol >> 8) & 0xff) / 255.0;

        pcBoundSwitch = new SoSwitch();
        auto pBoundingSep = new SoSeparator();
        auto lineStyle = new SoDrawStyle;
        lineStyle->lineWidth = 2.0f;
        pBoundingSep->addChild(lineStyle);

        pcBoundColor->rgb.setValue(r, g, b);
        pBoundingSep->addChild(pcBoundColor);
        auto font = new SoFont();
        font->size.setValue(getBoundBoxFontSize());
        pBoundingSep->addChild(font);

        pBoundingSep->addChild(new SoResetTransform());
        pBoundingSep->addChild(pcBoundingBox);
        pcBoundingBox->coordsOn.setValue(false);
        pcBoundingBox->dimensionsOn.setValue(true);

        // add to the highlight node
        pcBoundSwitch->addChild(pBoundingSep);
        pcRoot->addChild(pcBoundSwitch);
    }

    if (pcBoundSwitch) {
        pcBoundSwitch->whichChild = (show ? 0 : -1);
    }
}

void ViewProviderGeometryObject::setSelectable(bool selectable)
{
    SoSearchAction sa;
    sa.setInterest(SoSearchAction::ALL);
    sa.setSearchingAll(true);
    sa.setType(Gui::SoFCSelection::getClassTypeId());
    sa.apply(pcRoot);

    SoPathList& pathList = sa.getPaths();

    for (int i = 0; i < pathList.getLength(); i++) {
        auto selNode = dynamic_cast<SoFCSelection*>(pathList[i]->getTail());
        if (selectable) {
            if (selNode) {
                selNode->selectionMode = SoFCSelection::SEL_ON;
                selNode->highlightMode = SoFCSelection::AUTO;
            }
        }
        else {
            if (selNode) {
                selNode->selectionMode = SoFCSelection::SEL_OFF;
                selNode->highlightMode = SoFCSelection::OFF;
                selNode->selected = SoFCSelection::NOTSELECTED;
            }
        }
    }
}

PyObject* ViewProviderGeometryObject::getPyObject()
{
    if (!pyViewObject) {
        pyViewObject = new ViewProviderGeometryObjectPy(this);
    }
    pyViewObject->IncRef();
    return pyViewObject;
}

void ViewProviderGeometryObject::handleChangedPropertyName(Base::XMLReader& reader,
                                                           const char* TypeName,
                                                           const char* PropName)
{
    if (strcmp(PropName, "ShapeColor") == 0
        && strcmp(TypeName, App::PropertyColor::getClassTypeId().getName()) == 0) {
        App::PropertyColor prop;
        prop.Restore(reader);
        ShapeAppearance.setDiffuseColor(prop.getValue());
    }
    else if (strcmp(PropName, "ShapeMaterial") == 0
             && strcmp(TypeName, App::PropertyMaterial::getClassTypeId().getName()) == 0) {
        App::PropertyMaterial prop;
        prop.Restore(reader);
        ShapeAppearance.setValue(prop.getValue());
    }
    else {
        App::PropertyContainer::handleChangedPropertyName(reader, TypeName, PropName);
    }
}
