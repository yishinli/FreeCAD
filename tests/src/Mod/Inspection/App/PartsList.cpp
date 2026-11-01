// SPDX-License-Identifier: LGPL-2.1-or-later

#include <gtest/gtest.h>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include <App/Document.h>
#include <App/Link.h>
#include <App/Part.h>
#include <App/PropertyLinks.h>
#include <Base/Placement.h>
#include <Mod/Inspection/App/PartsList.h>
#include <Mod/Part/App/PartFeature.h>
#include <src/App/InitApplication.h>

using Inspection::InterferenceCheck;
using Inspection::PartsList;

class PartsListTest: public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        tests::initApplication();
    }

    void SetUp() override
    {
        doc = App::GetApplication().newDocument("PartsList");
    }

    void TearDown() override
    {
        App::GetApplication().closeDocument(doc->getName());
    }

    Part::Feature* add(const char* name, const TopoDS_Shape& shape, const Base::Placement& at = {})
    {
        auto feature = doc->addObject<Part::Feature>(name);
        feature->Shape.setValue(shape);
        feature->Placement.setValue(at);
        return feature;
    }

    static TopoDS_Shape box(double x, double y, double z)
    {
        return BRepPrimAPI_MakeBox(x, y, z).Solid();
    }

    // Three arms of different lengths from one corner: not its own mirror image
    static TopoDS_Shape corner()
    {
        TopoDS_Shape shape = BRepAlgoAPI_Fuse(box(30, 2, 2), box(2, 20, 2)).Shape();
        return BRepAlgoAPI_Fuse(shape, box(2, 2, 10)).Shape();
    }

    static TopoDS_Shape mirrored(const TopoDS_Shape& shape)
    {
        gp_Trsf mirror;
        mirror.SetMirror(gp_Ax2(gp_Pnt(), gp_Dir(1, 0, 0)));
        return BRepBuilderAPI_Transform(shape, mirror, true).Shape();
    }

    // Two arms at the ends of a bar, one bent up and one down: a half turn about the bar's
    // middle maps it onto itself, a mirror does not
    static TopoDS_Shape twisted()
    {
        auto at = [](double x, double y, double z, double dx, double dy, double dz) {
            return TopoDS_Shape(BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), dx, dy, dz).Solid());
        };
        TopoDS_Shape shape = BRepAlgoAPI_Fuse(at(-10, -1, -1, 20, 2, 2), at(8, 0, -1, 2, 8, 2)).Shape();
        shape = BRepAlgoAPI_Fuse(shape, at(8, 6, -1, 2, 2, 7)).Shape();
        shape = BRepAlgoAPI_Fuse(shape, at(-10, -8, -1, 2, 8, 2)).Shape();
        return BRepAlgoAPI_Fuse(shape, at(-10, -8, -1, 2, 2, 7)).Shape();
    }

    std::vector<Inspection::PartsListItem> list(bool visibleOnly = true)
    {
        parts = InterferenceCheck::collectParts(doc, nullptr, visibleOnly);
        return PartsList::group(parts);
    }

    App::Document* doc {};
    std::vector<Inspection::InterferencePart> parts;
};

// NOLINTBEGIN
TEST_F(PartsListTest, movedAndTurnedCopiesAreOneItem)
{
    add("Plate", box(40, 20, 5));
    add("Plate001",
        box(40, 20, 5),
        Base::Placement(Base::Vector3d(100, 0, 0), Base::Rotation(Base::Vector3d(0, 0, 1), 1.0)));
    add("Block", box(10, 10, 10));
    doc->recompute();

    auto items = list();

    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].name, "Plate");
    EXPECT_EQ(items[0].instances.size(), 2u);
    EXPECT_NEAR(items[0].volume, 4000.0, 1e-6);
    EXPECT_EQ(items[1].name, "Block");
    EXPECT_EQ(items[1].instances.size(), 1u);
}

TEST_F(PartsListTest, separateObjectsOfOneShapeAreOneItem)
{
    // As an imported assembly has them: no links, labels with copy numbers
    add("Screw", corner())->Label.setValue("M3x8");
    add("Screw001", corner(), Base::Placement(Base::Vector3d(50, 0, 0), Base::Rotation()))
        ->Label.setValue("M3x8001");
    add("Screw002",
        corner(),
        Base::Placement(Base::Vector3d(0, 50, 0), Base::Rotation(Base::Vector3d(1, 1, 0), 2.0)))
        ->Label.setValue("M3x8002");
    doc->recompute();

    auto items = list();

    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].name, "M3x8");
    EXPECT_EQ(items[0].instances.size(), 3u);
    EXPECT_FALSE(items[0].mirrored);
}

TEST_F(PartsListTest, linksCountAsTheirObject)
{
    auto pin = add("Pin", box(4, 4, 20));
    for (int i = 0; i < 3; ++i) {
        auto link = doc->addObject<App::Link>(("PinLink" + std::to_string(i)).c_str());
        link->setLink(-1, pin);
        link->Placement.setValue(
            Base::Placement(Base::Vector3d(10.0 * (i + 1), 0, 0), Base::Rotation())
        );
    }
    doc->recompute();

    auto items = list();

    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].instances.size(), 4u);
}

TEST_F(PartsListTest, mirrorImageIsAnItemOfItsOwn)
{
    add("Bracket", corner());
    add("Bracket001",
        mirrored(corner()),
        Base::Placement(Base::Vector3d(100, 0, 0), Base::Rotation()));
    add("Bracket002",
        corner(),
        Base::Placement(Base::Vector3d(0, 100, 0), Base::Rotation(Base::Vector3d(0, 1, 1), 0.7)));
    doc->recompute();

    auto items = list();

    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].name, "Bracket");
    EXPECT_EQ(items[0].instances.size(), 2u);
    EXPECT_FALSE(items[0].mirrored);
    EXPECT_EQ(items[1].name, "Bracket (mirrored)");
    EXPECT_TRUE(items[1].mirrored);
    EXPECT_EQ(items[1].instances.size(), 1u);
}

TEST_F(PartsListTest, mirroredSymmetricPartIsTheSameItem)
{
    add("Block", box(10, 20, 30));
    add("Block001",
        mirrored(box(10, 20, 30)),
        Base::Placement(Base::Vector3d(100, 0, 0), Base::Rotation()));
    doc->recompute();

    auto items = list();

    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].instances.size(), 2u);
}

TEST_F(PartsListTest, hiddenPartsButNotLinkSources)
{
    auto source = add("Pin", box(4, 4, 20));
    source->Visibility.setValue(false);
    for (int i = 0; i < 2; ++i) {
        auto link = doc->addObject<App::Link>(("PinLink" + std::to_string(i)).c_str());
        link->setLink(-1, source);
        link->Placement.setValue(
            Base::Placement(Base::Vector3d(10.0 * (i + 1), 0, 0), Base::Rotation())
        );
    }
    add("Spare", box(7, 7, 7))->Visibility.setValue(false);
    doc->recompute();

    auto visible = list(true);
    ASSERT_EQ(visible.size(), 1u);
    EXPECT_EQ(visible[0].instances.size(), 2u);

    auto all = list(false);
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].instances.size(), 2u);  // the hidden source is not a third pin
    EXPECT_EQ(all[1].name, "Spare");
}

TEST_F(PartsListTest, sizeIsInThePartsOwnAxes)
{
    add("Bar",
        box(10, 20, 30),
        Base::Placement(Base::Vector3d(5, 5, 5), Base::Rotation(Base::Vector3d(1, 0, 0), 0.5)));
    doc->recompute();

    auto items = list();

    ASSERT_EQ(items.size(), 1u);
    EXPECT_NEAR(items[0].size.X(), 10.0, 1e-6);
    EXPECT_NEAR(items[0].size.Y(), 20.0, 1e-6);
    EXPECT_NEAR(items[0].size.Z(), 30.0, 1e-6);
}

TEST_F(PartsListTest, csvQuotesAndCounts)
{
    add("Plate", box(40, 20, 5))->Label.setValue("Plate, \"left\"");
    add("Plate001", box(40, 20, 5), Base::Placement(Base::Vector3d(100, 0, 0), Base::Rotation()));
    doc->recompute();

    auto items = list();
    const std::string text = PartsList::csv(items, parts);

    EXPECT_EQ(text.rfind("\xEF\xBB\xBFIndex,Name,", 0), 0u);
    EXPECT_NE(text.find("1,\"Plate, \"\"left\"\"\",,2,40.000,20.000,5.000,4000.000,"), std::string::npos);

    // Labels a spreadsheet would take for formulas
    doc->getObject("Plate")->Label.setValue("=1+1");
    items = list();
    EXPECT_NE(PartsList::csv(items, parts).find(",'=1+1,"), std::string::npos);
}
TEST_F(PartsListTest, hiddenInputsInAContainerAreNotParts)
{
    auto container = doc->addObject<App::Part>("Assembly");
    auto stock = add("Stock", box(20, 20, 10));
    auto hole = add("Hole", box(4, 4, 10), Base::Placement(Base::Vector3d(8, 8, 0), Base::Rotation()));
    auto cut = doc->addObject("Part::Cut", "Cut");
    ASSERT_NE(cut, nullptr);
    static_cast<App::PropertyLink*>(cut->getPropertyByName("Base"))->setValue(stock);
    static_cast<App::PropertyLink*>(cut->getPropertyByName("Tool"))->setValue(hole);
    stock->Visibility.setValue(false);
    hole->Visibility.setValue(false);
    container->addObject(stock);
    container->addObject(hole);
    container->addObject(cut);
    // A hidden source with its link beside it, and a hidden spare nothing uses
    auto pin = add("Pin", box(2, 2, 10), Base::Placement(Base::Vector3d(50, 0, 0), Base::Rotation()));
    pin->Visibility.setValue(false);
    auto link = doc->addObject<App::Link>("PinLink");
    link->setLink(-1, pin);
    link->Placement.setValue(Base::Placement(Base::Vector3d(60, 0, 0), Base::Rotation()));
    container->addObject(pin);
    container->addObject(link);
    auto spare
        = add("Spare", box(3, 3, 3), Base::Placement(Base::Vector3d(80, 0, 0), Base::Rotation()));
    spare->Visibility.setValue(false);
    container->addObject(spare);
    doc->recompute();

    auto items = list(false);

    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0].name, "Cut");
    EXPECT_EQ(items[0].instances.size(), 1u);
    EXPECT_EQ(items[1].name, "Pin");
    EXPECT_EQ(items[1].instances.size(), 1u);
    EXPECT_EQ(items[2].name, "Spare");
}

TEST_F(PartsListTest, halfTurnSymmetricMirrorImageIsApart)
{
    add("Bracket", twisted());
    add("Bracket001",
        twisted(),
        Base::Placement(Base::Vector3d(0, 50, 0), Base::Rotation(Base::Vector3d(1, 2, 3), 1.1)));
    add("Bracket002",
        mirrored(twisted()),
        Base::Placement(Base::Vector3d(50, 0, 0), Base::Rotation()));
    doc->recompute();

    auto items = list();

    ASSERT_EQ(items.size(), 2u);
    EXPECT_EQ(items[0].instances.size(), 2u);
    EXPECT_EQ(items[1].name, "Bracket (mirrored)");
}

TEST_F(PartsListTest, numbersOfTheirOwnAreNotCopyNumbers)
{
    add("A", box(5, 5, 5))->Label.setValue("Bolt2024");
    add("B", box(5, 5, 5), Base::Placement(Base::Vector3d(10, 0, 0), Base::Rotation()))
        ->Label.setValue("Bolt2025");
    doc->recompute();

    auto items = list();

    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].name, "Bolt2024");
}
// NOLINTEND
