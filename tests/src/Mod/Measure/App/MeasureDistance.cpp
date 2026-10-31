// SPDX-License-Identifier: LGPL-2.1-or-later

#include <src/App/InitApplication.h>
#include <App/Document.h>
#include <App/Part.h>
#include <App/GeoFeatureGroupExtension.h>
#include <App/Link.h>
#include <App/MeasureManager.h>
#include <Mod/Measure/App/MeasureDistance.h>
#include <Mod/Part/App/PartFeature.h>
#include <Base/Placement.h>
#include <Base/Rotation.h>
#include <Base/Vector3D.h>
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <gp_Circ.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>

class MeasureDistance: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        document = App::GetApplication().newDocument("Measure");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(document->getName());
    }

    App::Document* getDocument() const
    {
        return document;
    }

    // The side face of a cylinder only, so that it is Face1
    Part::Feature* addCylinderFace(const char* name, const gp_Ax2& axis, double radius) const
    {
        auto feature = document->addObject<Part::Feature>(name);
        feature->Shape.setValue(BRepPrimAPI_MakeCylinder(axis, radius, 10.0).Cylinder().LateralFace());
        return feature;
    }

    Part::Feature* addCylinderZ(const char* name, double x, double radius) const
    {
        return addCylinderFace(name, gp_Ax2(gp_Pnt(x, 0.0, 0.0), gp::DZ()), radius);
    }

    TopoDS_Edge makeCircle(const gp_Pnt& pnt) const
    {
        gp_Circ circle;
        circle.SetLocation(pnt);
        circle.SetRadius(1.0);
        BRepBuilderAPI_MakeEdge mkEdge(circle);
        return mkEdge.Edge();
    }

private:
    App::Document* document {};
};

// NOLINTBEGIN
TEST_F(MeasureDistance, testCurvedFaceValidSelection)
{
    // Regression: https://github.com/FreeCAD/FreeCAD/issues/29235
    App::Document* doc = getDocument();

    auto sphere = doc->addObject<Part::Feature>("Sphere");
    sphere->Shape.setValue(BRepPrimAPI_MakeSphere(5.0).Solid());

    auto box = doc->addObject<Part::Feature>("Box");
    box->Shape.setValue(BRepPrimAPI_MakeBox(gp_Pnt(20.0, 0.0, 0.0), 10.0, 10.0, 10.0).Solid());

    doc->recompute();

    App::MeasureSelectionItem item1 {App::SubObjectT {sphere, "Face1"}, Base::Vector3d {}};
    App::MeasureSelectionItem item2 {App::SubObjectT {box, "Face1"}, Base::Vector3d {}};

    EXPECT_TRUE(Measure::MeasureDistance::isValidSelection({item1, item2}));
}

TEST_F(MeasureDistance, testCurvedFaceDistance)
{
    // Regression: https://github.com/FreeCAD/FreeCAD/issues/29235
    App::Document* doc = getDocument();

    auto sphere = doc->addObject<Part::Feature>("Sphere");
    sphere->Shape.setValue(BRepPrimAPI_MakeSphere(5.0).Solid());

    auto box = doc->addObject<Part::Feature>("Box");
    box->Shape.setValue(BRepPrimAPI_MakeBox(gp_Pnt(20.0, 0.0, 0.0), 10.0, 10.0, 10.0).Solid());

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(sphere, {"Face1"});
    md->Element2.setValue(box, {"Face1"});

    doc->recompute();

    EXPECT_NEAR(md->Distance.getValue(), 15.0, 1e-6);
}

TEST_F(MeasureDistance, testCircleCircle)
{
    App::Document* doc = getDocument();
    auto p1 = doc->addObject<Part::Feature>("Shape1");
    p1->Shape.setValue(makeCircle(gp_Pnt(0.0, 0.0, 0.0)));
    auto p2 = doc->addObject<Part::Feature>("Shape2");
    p2->Shape.setValue(makeCircle(gp_Pnt(3.0, 4.0, 0.0)));

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(p1, {"Edge1"});
    md->Element2.setValue(p2, {"Edge1"});

    doc->recompute();

    EXPECT_DOUBLE_EQ(md->Distance.getValue(), 5.0);
    EXPECT_DOUBLE_EQ(md->DistanceX.getValue(), 3.0);
    EXPECT_DOUBLE_EQ(md->DistanceY.getValue(), 4.0);
    EXPECT_DOUBLE_EQ(md->DistanceZ.getValue(), 0.0);
    EXPECT_EQ(md->Position1.getValue(), Base::Vector3d(0.0, 0.0, 0.0));
    EXPECT_EQ(md->Position2.getValue(), Base::Vector3d(3.0, 4.0, 0.0));
}

TEST_F(MeasureDistance, testCircleCircleWithPlacement)
{
    // Baseline for issue #30365: distance and the reported positions must respect
    // each object's own top-level Placement. Worked *before* #30423.
    App::Document* doc = getDocument();
    auto p1 = doc->addObject<Part::Feature>("Shape1");
    p1->Shape.setValue(makeCircle(gp_Pnt(0.0, 0.0, 0.0)));
    p1->Placement.setValue(Base::Placement(Base::Vector3d(10.0, 0.0, 0.0), Base::Rotation()));
    auto p2 = doc->addObject<Part::Feature>("Shape2");
    p2->Shape.setValue(makeCircle(gp_Pnt(0.0, 0.0, 0.0)));
    p2->Placement.setValue(Base::Placement(Base::Vector3d(10.0, 0.0, 5.0), Base::Rotation()));

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(p1, {"Edge1"});
    md->Element2.setValue(p2, {"Edge1"});

    doc->recompute();

    EXPECT_DOUBLE_EQ(md->Distance.getValue(), 5.0);
    EXPECT_DOUBLE_EQ(md->DistanceX.getValue(), 0.0);
    EXPECT_DOUBLE_EQ(md->DistanceY.getValue(), 0.0);
    EXPECT_DOUBLE_EQ(md->DistanceZ.getValue(), 5.0);
    EXPECT_EQ(md->Position1.getValue(), Base::Vector3d(10.0, 0.0, 0.0));
    EXPECT_EQ(md->Position2.getValue(), Base::Vector3d(10.0, 0.0, 5.0));
}

TEST_F(MeasureDistance, testTwoBoxesMovedByContainers)
{
    // Regression: https://github.com/FreeCAD/FreeCAD/issues/30365
    // Mirrors the issue report: two bodies built at the origin and then moved. Requires #30423 fix.
    App::Document* doc = getDocument();

    auto box1 = doc->addObject<Part::Feature>("Box1");
    box1->Shape.setValue(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Solid());
    auto container1 = doc->addObject<App::Part>("Container1");
    container1->getExtensionByType<App::GeoFeatureGroupExtension>()->addObject(box1);

    auto box2 = doc->addObject<Part::Feature>("Box2");
    box2->Shape.setValue(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Solid());
    auto container2 = doc->addObject<App::Part>("Container2");
    container2->Placement.setValue(Base::Placement(Base::Vector3d(50.0, 0.0, 0.0), Base::Rotation()));
    container2->getExtensionByType<App::GeoFeatureGroupExtension>()->addObject(box2);

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(container1, {"Box1.Vertex1"});
    md->Element2.setValue(container2, {"Box2.Vertex1"});

    doc->recompute();

    EXPECT_DOUBLE_EQ(md->Distance.getValue(), 50.0);
    EXPECT_DOUBLE_EQ(md->DistanceX.getValue(), 50.0);
    EXPECT_DOUBLE_EQ(md->DistanceY.getValue(), 0.0);
    EXPECT_DOUBLE_EQ(md->DistanceZ.getValue(), 0.0);
}

TEST_F(MeasureDistance, testLinkedCompoundKeepsInternalLocation)
{
    // Centre = link (100,0,0) + the edge's own (0,0,20); the other circle sits at (100,0,0).
    App::Document* doc = getDocument();

    TopoDS_Edge edge = makeCircle(gp_Pnt(0.0, 0.0, 0.0));
    gp_Trsf trsf;
    trsf.SetTranslation(gp_Vec(0.0, 0.0, 20.0));
    edge.Move(TopLoc_Location(trsf));
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, edge);

    auto feature = doc->addObject<Part::Feature>("Shape");
    feature->Shape.setValue(compound);
    auto link = doc->addObject<App::Link>("Link");
    link->setLink(-1, feature);
    link->LinkPlacement.setValue(Base::Placement(Base::Vector3d(100.0, 0.0, 0.0), Base::Rotation()));

    auto circle = doc->addObject<Part::Feature>("Circle");
    circle->Shape.setValue(makeCircle(gp_Pnt(100.0, 0.0, 0.0)));

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(link, {"Edge1"});
    md->Element2.setValue(circle, {"Edge1"});

    doc->recompute();

    EXPECT_DOUBLE_EQ(md->Distance.getValue(), 20.0);
    EXPECT_EQ(md->Position1.getValue(), Base::Vector3d(100.0, 0.0, 20.0));
    EXPECT_EQ(md->Position2.getValue(), Base::Vector3d(100.0, 0.0, 0.0));
}

TEST_F(MeasureDistance, testCylinderCylinderAxis)
{
    // Two holes measure between their axes, not between their walls
    App::Document* doc = getDocument();
    auto c1 = addCylinderZ("Cylinder1", 0.0, 2.0);
    auto c2 = addCylinderZ("Cylinder2", 10.0, 3.0);

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(c1, {"Face1"});
    md->Element2.setValue(c2, {"Face1"});

    doc->recompute();

    EXPECT_NEAR(md->Distance.getValue(), 10.0, 1e-9);
    EXPECT_NEAR(md->DistanceX.getValue(), 10.0, 1e-9);
    EXPECT_NEAR(md->DistanceZ.getValue(), 0.0, 1e-9);
    EXPECT_NEAR(md->Position1.getValue().z, 5.0, 1e-9);
}

TEST_F(MeasureDistance, testCylindersNearlyParallelAxis)
{
    // Axes off by far more than Precision::Angular(), as in imported or rotated parts
    App::Document* doc = getDocument();
    auto c1 = addCylinderZ("Cylinder1", 0.0, 2.0);
    const double tilt = 1e-9;
    auto c2 = addCylinderFace(
        "Cylinder2",
        gp_Ax2(gp_Pnt(10.0, 0.0, 0.0), gp_Dir(std::sin(tilt), 0.0, std::cos(tilt))),
        3.0
    );

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(c1, {"Face1"});
    md->Element2.setValue(c2, {"Face1"});

    doc->recompute();

    EXPECT_NEAR(md->Distance.getValue(), 10.0, 1e-6);
}

TEST_F(MeasureDistance, testFilletIsNotRound)
{
    // A quarter cylinder, as a fillet: measured to its material, not its axis
    App::Document* doc = getDocument();
    auto fillet = doc->addObject<Part::Feature>("Fillet");
    fillet->Shape.setValue(
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(), gp::DZ()), 2.0, 10.0, std::numbers::pi / 2)
            .Cylinder()
            .LateralFace()
    );
    auto half = doc->addObject<Part::Feature>("Half");
    half->Shape.setValue(
        BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(10.0, 0.0, 0.0), gp::DZ()), 3.0, 10.0, std::numbers::pi)
            .Cylinder()
            .LateralFace()
    );
    auto box = doc->addObject<Part::Feature>("Box");
    box->Shape.setValue(BRepPrimAPI_MakeBox(gp_Pnt(20.0, -5.0, 0.0), 10.0, 10.0, 10.0).Solid());

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(fillet, {"Face1"});
    md->Element2.setValue(box, {"Face1"});
    auto halfToBox = doc->addObject<Measure::MeasureDistance>("HalfToBox");
    halfToBox->Element1.setValue(half, {"Face1"});
    halfToBox->Element2.setValue(box, {"Face1"});

    doc->recompute();

    EXPECT_NEAR(md->Distance.getValue(), 18.0, 1e-6);         // from the fillet surface at x = 2
    EXPECT_NEAR(halfToBox->Distance.getValue(), 10.0, 1e-9);  // a half hole is still round
}

TEST_F(MeasureDistance, testCircleCylinderAxis)
{
    App::Document* doc = getDocument();
    auto circle = doc->addObject<Part::Feature>("Circle");
    circle->Shape.setValue(makeCircle(gp_Pnt(3.0, 4.0, 2.0)));
    auto cylinder = addCylinderZ("Cylinder", 0.0, 1.5);

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(circle, {"Edge1"});
    md->Element2.setValue(cylinder, {"Face1"});

    doc->recompute();

    EXPECT_NEAR(md->Distance.getValue(), 5.0, 1e-9);
    EXPECT_EQ(md->Position1.getValue(), Base::Vector3d(3.0, 4.0, 2.0));
    EXPECT_TRUE(md->Position2.getValue().IsEqual(Base::Vector3d(0.0, 0.0, 2.0), 1e-9));
}

TEST_F(MeasureDistance, testCylinderPlaneAxis)
{
    // A hole to a face parallel to its axis: the axis to the face
    App::Document* doc = getDocument();
    auto cylinder = addCylinderZ("Cylinder", 0.0, 2.0);
    auto box = doc->addObject<Part::Feature>("Box");
    box->Shape.setValue(BRepPrimAPI_MakeBox(gp_Pnt(20.0, -5.0, 0.0), 10.0, 10.0, 10.0).Solid());

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(cylinder, {"Face1"});
    md->Element2.setValue(box, {"Face1"});  // the face at x = 20

    doc->recompute();

    EXPECT_NEAR(md->Distance.getValue(), 20.0, 1e-9);
    EXPECT_NEAR(md->DistanceX.getValue(), 20.0, 1e-9);
}

TEST_F(MeasureDistance, testVertexCylinderAxis)
{
    App::Document* doc = getDocument();
    auto vertex = doc->addObject<Part::Feature>("Vertex");
    vertex->Shape.setValue(BRepBuilderAPI_MakeVertex(gp_Pnt(5.0, 0.0, 3.0)).Vertex());
    auto cylinder = addCylinderZ("Cylinder", 0.0, 2.0);

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(cylinder, {"Face1"});
    md->Element2.setValue(vertex, {"Vertex1"});

    doc->recompute();

    EXPECT_NEAR(md->Distance.getValue(), 5.0, 1e-9);
    EXPECT_TRUE(md->Position1.getValue().IsEqual(Base::Vector3d(0.0, 0.0, 3.0), 1e-9));
    EXPECT_EQ(md->Position2.getValue(), Base::Vector3d(5.0, 0.0, 3.0));
}

TEST_F(MeasureDistance, testCylindersNotParallelUseMinimum)
{
    // Axes at an angle have no single axis distance: the closest walls are measured
    App::Document* doc = getDocument();
    auto c1 = addCylinderZ("Cylinder1", 0.0, 2.0);
    auto c2 = addCylinderFace("Cylinder2", gp_Ax2(gp_Pnt(-5.0, 0.0, 20.0), gp::DX()), 1.0);

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(c1, {"Face1"});
    md->Element2.setValue(c2, {"Face1"});

    doc->recompute();

    EXPECT_NEAR(md->Distance.getValue(), 9.0, 1e-6);
}

TEST_F(MeasureDistance, testPlanePlaneUnchanged)
{
    // Elements that are not round keep the minimum distance
    App::Document* doc = getDocument();
    auto box = doc->addObject<Part::Feature>("Box");
    box->Shape.setValue(BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Solid());

    auto md = doc->addObject<Measure::MeasureDistance>("Distance");
    md->Element1.setValue(box, {"Face1"});
    md->Element2.setValue(box, {"Face2"});

    doc->recompute();

    EXPECT_NEAR(md->Distance.getValue(), 10.0, 1e-9);
}
// NOLINTEND
