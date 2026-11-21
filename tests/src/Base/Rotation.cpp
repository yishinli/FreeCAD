#include <algorithm>
#include <cmath>
#include <numbers>

#include <gtest/gtest.h>
#include <Base/Exception.h>
#include <Base/Matrix.h>
#include <Base/Rotation.h>


// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
TEST(Rotation, TestNonUniformScaleLeft)
{
    Base::Rotation rot;
    rot.setYawPitchRoll(45.0, 0.0, 0.0);

    Base::Matrix4D mat;
    rot.getValue(mat);

    Base::Matrix4D scale;
    scale.scale(1.0, std::sqrt(3.0), 1.0);

    Base::Rotation scaled_rot(scale * mat);

    rot.setYawPitchRoll(60.0, 0.0, 0.0);
    EXPECT_EQ(scaled_rot.isSame(rot, 1.0e-7), true);
    EXPECT_EQ(rot.isSame(scaled_rot, 1.0e-7), true);
}

TEST(Rotation, TestNonUniformScaleRight)
{
    Base::Rotation rot;
    rot.setYawPitchRoll(20.0, 0.0, 0.0);

    Base::Matrix4D mat;
    rot.getValue(mat);

    Base::Matrix4D scale;
    scale.scale(2.0, 3.0, 4.0);

    Base::Rotation scaled_rot(mat * scale);

    EXPECT_EQ(scaled_rot.isSame(rot, 1.0e-7), true);
    EXPECT_EQ(rot.isSame(scaled_rot, 1.0e-7), true);
}

TEST(Rotation, TestUniformScaleGT1)
{
    Base::Rotation rot;
    rot.setYawPitchRoll(20.0, 0.0, 0.0);

    Base::Matrix4D mat;
    rot.getValue(mat);

    Base::Matrix4D scale;
    scale.scale(3.0, 3.0, 3.0);

    Base::Rotation scaled_rot(mat * scale);

    EXPECT_EQ(scaled_rot.isSame(rot, 1.0e-7), true);
    EXPECT_EQ(rot.isSame(scaled_rot, 1.0e-7), true);
}

TEST(Rotation, TestUniformScaleLT1)
{
    Base::Matrix4D mat;
    mat.scale(0.5);

    Base::Rotation scaled_rot(mat);

    EXPECT_EQ(scaled_rot.isSame(scaled_rot, 1.0e-7), true);
}

TEST(Rotation, TestRotationDecompose)
{
    Base::Matrix4D mat;
    mat.setCol(0, Base::Vector3d {1, 0, 0});
    mat.setCol(1, Base::Vector3d {1, 1, 0});
    mat.setCol(2, Base::Vector3d {0, 0, 1});

    // decompose rotation part
    EXPECT_TRUE(Base::Rotation {mat}.isIdentity());
}
namespace
{
// What a document saves and restores a rotation from: its raw axis and angle
Base::Rotation restored(const Base::Rotation& rot)
{
    Base::Vector3d axis;
    double angle {};
    rot.getRawValue(axis, angle);
    return {axis, angle};
}

void expectSameQuaternion(const Base::Rotation& a, const Base::Rotation& b, double tol)
{
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(a[i], b[i], tol) << "quaternion component " << i;
    }
}
}  // namespace

TEST(Rotation, SmallAngleFromQuaternionIsAccurate)
{
    // A rotation as a STEP import gives it: about 1.75e-7 rad around z
    const Base::Rotation rot(0.0, 0.0, 8.742277654572034e-08, 0.9999999999999961);
    Base::Vector3d axis;
    double angle {};
    rot.getRawValue(axis, angle);

    EXPECT_NEAR(angle, 2.0 * std::atan2(rot[2], rot[3]), 1e-21);
    EXPECT_NEAR(axis.x, 0.0, 1e-15);
    EXPECT_NEAR(axis.y, 0.0, 1e-15);
    EXPECT_NEAR(axis.z, 1.0, 1e-15);
    expectSameQuaternion(restored(rot), rot, 1e-16);
}

TEST(Rotation, SmallAngleWithUnitScalarPartIsKept)
{
    // w normalizes to exactly 1.0; the rotation used to be dropped to the identity
    const Base::Rotation rot(0.0, 0.0, 1e-9, 1.0);
    ASSERT_EQ(rot[3], 1.0);
    ASSERT_GT(rot[2], 0.0);
    expectSameQuaternion(restored(rot), rot, 1e-20);
}

TEST(Rotation, AngleNearFullTurnKeepsQuaternion)
{
    const Base::Rotation rot(0.0, 0.0, 1e-8, -1.0);
    Base::Vector3d axis;
    double angle {};
    rot.getRawValue(axis, angle);

    EXPECT_GT(angle, std::numbers::pi);
    EXPECT_LE(angle, 2.0 * std::numbers::pi);
    expectSameQuaternion(restored(rot), rot, 1e-16);
}

TEST(Rotation, AnglesFromQuaternionRoundTrip)
{
    for (double angle : {1e-12, 1e-9, 1e-6, 1e-3, 0.5, 1.0, 3.0, 3.14159, 4.0, 6.0, 6.283185}) {
        const Base::Vector3d axis(0.3, -0.5, 0.8);
        Base::Rotation byAxis(axis, angle);
        // Set it again from its quaternion, as matrix and STEP rotations are
        const Base::Rotation rot(byAxis[0], byAxis[1], byAxis[2], byAxis[3]);
        Base::Vector3d raw;
        double rawAngle {};
        rot.getRawValue(raw, rawAngle);
        EXPECT_NEAR(rawAngle, angle, 1e-15 * std::max(1.0, angle)) << "angle " << angle;
        // A couple of units in the last place of 1
        expectSameQuaternion(restored(rot), rot, 4e-16);
    }
}

TEST(Rotation, IdentityHasZAxis)
{
    const Base::Rotation rot(0.0, 0.0, 0.0, 1.0);
    Base::Vector3d axis;
    double angle {};
    rot.getRawValue(axis, angle);
    EXPECT_EQ(angle, 0.0);
    EXPECT_EQ(axis, Base::Vector3d(0.0, 0.0, 1.0));
}

TEST(Rotation, QuaternionKeepsRawAxisLength)
{
    Base::Rotation rot(Base::Vector3d(1.0, 2.0, 3.0), 0.5);
    rot.setValue(0.0, 0.0, std::sin(0.1), std::cos(0.1));
    Base::Vector3d axis;
    double angle {};
    rot.getRawValue(axis, angle);
    EXPECT_NEAR(axis.Length(), std::sqrt(14.0), 1e-14);
    EXPECT_NEAR(angle, 0.2, 1e-15);
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
