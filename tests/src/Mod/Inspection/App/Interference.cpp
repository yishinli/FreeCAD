// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <BRep_Builder.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <ShapeFix_ShapeTolerance.hxx>
#include <TopoDS_Compound.hxx>

#include <App/Document.h>
#include <App/Link.h>
#include <App/Part.h>
#include <Base/Placement.h>
#include <Mod/Inspection/App/Interference.h>
#include <Mod/Part/App/PartFeature.h>
#include <src/App/InitApplication.h>

using Inspection::InterferenceCheck;
using Inspection::InterferenceKind;
using Inspection::InterferenceSettings;

class Interference: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        doc = App::GetApplication().newDocument("Interference");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(doc->getName());
    }

    Part::Feature* addBox(const char* name, double x, double y = 0.0, double z = 0.0, double size = 10.0)
    {
        auto feature = doc->addObject<Part::Feature>(name);
        feature->Shape.setValue(BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), size, size, size).Solid());
        return feature;
    }

    // Solids of the given corners and sizes as one compound part
    Part::Feature* addCompound(const char* name, const std::vector<std::pair<gp_Pnt, double>>& boxes)
    {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        for (const auto& [corner, size] : boxes) {
            builder.Add(compound, BRepPrimAPI_MakeBox(corner, size, size, size).Solid());
        }
        auto feature = doc->addObject<Part::Feature>(name);
        feature->Shape.setValue(compound);
        return feature;
    }

    // Every part against every other
    Inspection::InterferenceReport checkAll(const InterferenceSettings& settings = {})
    {
        parts = InterferenceCheck::collectParts(doc);
        std::vector<std::size_t> all(parts.size());
        for (std::size_t i = 0; i < all.size(); ++i) {
            all[i] = i;
        }
        return InterferenceCheck::check(parts, all, {}, settings);
    }

    App::Document* doc {};
    std::vector<Inspection::InterferencePart> parts;
};

// NOLINTBEGIN
TEST_F(Interference, overlappingBoxesClash)
{
    addBox("A", 0.0);
    addBox("B", 5.0);

    auto report = checkAll();

    ASSERT_EQ(report.results.size(), 1u);
    const auto& result = report.results[0];
    EXPECT_EQ(result.kind, InterferenceKind::Clash);
    EXPECT_NEAR(result.volume, 500.0, 1e-6);
    EXPECT_NEAR(result.point1.X(), 7.5, 1e-6);
    EXPECT_FALSE(result.shared.IsNull());
}

TEST_F(Interference, partInsideAnotherClashes)
{
    addBox("Big", 0.0, 0.0, 0.0, 10.0);
    addBox("Small", 2.0, 2.0, 2.0, 2.0);

    auto report = checkAll();

    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(report.results[0].kind, InterferenceKind::Clash);
    EXPECT_NEAR(report.results[0].volume, 8.0, 1e-6);
}

TEST_F(Interference, touchingBoxesAreContact)
{
    addBox("A", 0.0);
    addBox("B", 10.0);

    auto report = checkAll();

    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(report.results[0].kind, InterferenceKind::Contact);
    EXPECT_NEAR(report.results[0].distance, 0.0, 1e-9);
}

TEST_F(Interference, gapIsReportedOnlyWithinClearance)
{
    addBox("A", 0.0);
    addBox("B", 12.0);

    EXPECT_TRUE(checkAll().results.empty());

    InterferenceSettings settings;
    settings.clearance = 3.0;
    auto report = checkAll(settings);
    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(report.results[0].kind, InterferenceKind::Clearance);
    EXPECT_NEAR(report.results[0].distance, 2.0, 1e-9);
    EXPECT_NEAR(report.results[0].point1.X(), 10.0, 1e-9);
    EXPECT_NEAR(report.results[0].point2.X(), 12.0, 1e-9);
}

TEST_F(Interference, distantPairsArePrunedByTheirBoxes)
{
    addBox("A", 0.0);
    addBox("B", 100.0);
    addBox("C", 200.0);

    auto report = checkAll();

    EXPECT_EQ(report.pairs, 3u);
    EXPECT_EQ(report.pruned, 3u);
    EXPECT_EQ(report.checked, 0u);
    EXPECT_TRUE(report.results.empty());
}

TEST_F(Interference, groupAgainstOthers)
{
    addBox("A", 0.0);
    addBox("B", 5.0);   // clashes with A and C
    addBox("C", 12.0);  // clashes with B only

    parts = InterferenceCheck::collectParts(doc);
    ASSERT_EQ(parts.size(), 3u);
    auto report = InterferenceCheck::check(parts, {2}, {0, 1}, {});

    EXPECT_EQ(report.pairs, 2u);
    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(parts[report.results[0].first].name, "B");
    EXPECT_EQ(parts[report.results[0].second].name, "C");
}

TEST_F(Interference, assemblyIsOpenedIntoItsParts)
{
    // Two boxes inside an App::Part that is moved: they clash where they stand
    auto container = doc->addObject<App::Part>("Assembly");
    container->addObject(addBox("A", 0.0));
    container->addObject(addBox("B", 5.0));
    container->Placement.setValue(Base::Placement(Base::Vector3d(100.0, 0.0, 0.0), Base::Rotation()));
    auto hidden = addBox("Hidden", 104.0);
    hidden->Visibility.setValue(false);
    doc->recompute();

    auto report = checkAll();

    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].name, "Assembly / A");
    EXPECT_EQ(parts[1].name, "Assembly / B");
    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(report.results[0].kind, InterferenceKind::Clash);
    EXPECT_NEAR(report.results[0].point1.X(), 107.5, 1e-6);
}

TEST_F(Interference, linkIsAPartWhereItIsPlaced)
{
    auto box = addBox("A", 0.0);
    auto link = doc->addObject<App::Link>("Link");
    link->setLink(-1, box);
    link->Placement.setValue(Base::Placement(Base::Vector3d(5.0, 0.0, 0.0), Base::Rotation()));
    doc->recompute();

    auto report = checkAll();

    ASSERT_EQ(parts.size(), 2u);
    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(report.results[0].kind, InterferenceKind::Clash);
    EXPECT_NEAR(report.results[0].volume, 500.0, 1e-6);
}

TEST_F(Interference, selectionInsideAContainer)
{
    auto container = doc->addObject<App::Part>("Assembly");
    container->addObject(addBox("A", 0.0));
    container->addObject(addBox("B", 5.0));
    doc->recompute();

    // As a face of B picked in the 3D view: the top object and the path to the part
    auto picked = InterferenceCheck::collectParts({App::SubObjectT(container, "B.Face1")});

    ASSERT_EQ(picked.size(), 1u);
    EXPECT_EQ(picked[0].name, "Assembly / B");
}
TEST_F(Interference, partInsideACompoundClashes)
{
    // A plate made of two solids, and a pin wholly inside one of them
    addCompound("Plate", {{gp_Pnt(0.0, 0.0, 0.0), 10.0}, {gp_Pnt(20.0, 0.0, 0.0), 10.0}});
    addBox("Pin", 22.0, 2.0, 2.0, 2.0);

    auto report = checkAll();

    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(report.results[0].kind, InterferenceKind::Clash);
    EXPECT_NEAR(report.results[0].volume, 8.0, 1e-6);
}

TEST_F(Interference, compoundInsideAPartClashes)
{
    addBox("Block", 0.0, 0.0, 0.0, 10.0);
    addCompound("Pins", {{gp_Pnt(2.0, 2.0, 2.0), 2.0}, {gp_Pnt(50.0, 0.0, 0.0), 2.0}});

    auto report = checkAll();

    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(report.results[0].kind, InterferenceKind::Clash);
    EXPECT_NEAR(report.results[0].volume, 8.0, 1e-6);
}

TEST_F(Interference, gapWithinTheShapesToleranceIsContact)
{
    // Mated faces of imported parts are apart by less than their own tolerance
    addBox("A", 0.0);
    auto b = addBox("B", 10.0 + 1e-5);
    TopoDS_Shape shape = b->Shape.getValue();
    ShapeFix_ShapeTolerance().SetTolerance(shape, 1e-4);
    b->Shape.setValue(shape);

    auto report = checkAll();

    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(report.results[0].kind, InterferenceKind::Contact);
}

TEST_F(Interference, aLooseToleranceDoesNotHideAGap)
{
    addBox("A", 0.0);
    auto b = addBox("B", 10.5);
    TopoDS_Shape shape = b->Shape.getValue();
    ShapeFix_ShapeTolerance().SetTolerance(shape, 0.6);
    b->Shape.setValue(shape);

    InterferenceSettings settings;
    settings.clearance = 1.0;
    auto report = checkAll(settings);

    ASSERT_EQ(report.results.size(), 1u);
    EXPECT_EQ(report.results[0].kind, InterferenceKind::Clearance);
}

TEST_F(Interference, aPartPickedTwiceIsCheckedOnce)
{
    addBox("Box", 0.0);
    auto other = addBox("Other", 20.0);

    auto picked = InterferenceCheck::collectParts(
        {App::SubObjectT(doc->getObject("Box"), "Face1"),
         App::SubObjectT(doc->getObject("Box"), "Face2"),
         App::SubObjectT(other, "")}
    );
    ASSERT_EQ(picked.size(), 2u);
    auto report = InterferenceCheck::check(picked, {0, 1}, {}, {});
    EXPECT_TRUE(report.results.empty());
}
TEST_F(Interference, budgetLeavesTheRestUnchecked)
{
    // A row of boxes, each overlapping the next; no time at all
    for (int i = 0; i < 20; ++i) {
        addBox(("B" + std::to_string(i)).c_str(), 5.0 * i);
    }
    InterferenceSettings settings;
    settings.budget = 0.0;

    auto report = checkAll(settings);

    std::size_t unchecked = 0;
    for (const auto& result : report.results) {
        if (result.kind == InterferenceKind::Unchecked) {
            EXPECT_EQ(result.reason, "the time budget ran out");
            ++unchecked;
        }
    }
    // With no time, no pair whose boxes meet is measured: all are listed as unchecked
    EXPECT_EQ(report.checked, 0u);
    EXPECT_EQ(unchecked, report.pairs - report.pruned);
    EXPECT_GT(unchecked, 0u);
}

TEST_F(Interference, manyPairsGiveTheSameResultsInOrder)
{
    // Measured on several threads, listed in the order of the pairs
    for (int i = 0; i < 30; ++i) {
        addBox(("B" + std::to_string(i)).c_str(), 5.0 * i);
    }

    auto report = checkAll();

    ASSERT_EQ(report.results.size(), 29u + 28u);  // next box: clash; the one after: contact
    for (std::size_t k = 1; k < report.results.size(); ++k) {
        const auto& before = report.results[k - 1];
        const auto& after = report.results[k];
        EXPECT_TRUE(
            before.first < after.first || (before.first == after.first && before.second < after.second)
        );
    }
}
// NOLINTEND
