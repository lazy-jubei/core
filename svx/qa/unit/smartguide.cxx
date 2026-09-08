/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <tools/gen.hxx>
#include <svdsmguid.hxx>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>
#include <cstddef>
#include <vector>

namespace
{
/// Tests of the pure smart guide (dynamic alignment / equal size) geometry
class SmartGuideTest : public CppUnit::TestFixture
{
private:
    static tools::Rectangle rect(int nLeft, int nTop, int nRight, int nBottom)
    {
        return tools::Rectangle(Point(nLeft, nTop), Point(nRight, nBottom));
    }

public:
    CPPUNIT_TEST_SUITE(SmartGuideTest);
    // move: edge matches
    CPPUNIT_TEST(testMoveEdgeMatch);
    CPPUNIT_TEST(testMoveCenterMatch);
    CPPUNIT_TEST(testMoveNearestCandidateWins);
    CPPUNIT_TEST(testMoveToleranceBoundary);
    CPPUNIT_TEST(testMoveToleranceMiss);
    CPPUNIT_TEST(testMoveNegativeCoordinates);
    CPPUNIT_TEST(testMoveIndependentAxes);
    CPPUNIT_TEST(testMoveAlreadyAligned);
    CPPUNIT_TEST(testMoveAppliedDeltaInvariant);
    CPPUNIT_TEST(testMoveTieBreakStable);
    CPPUNIT_TEST(testMoveRounding);
    CPPUNIT_TEST(testMoveEmptyCandidates);
    // resize: equal size / edge / center matches
    CPPUNIT_TEST(testResizeSizeMatch);
    CPPUNIT_TEST(testResizeEdgeMatch);
    CPPUNIT_TEST(testResizeCrossFeature);
    CPPUNIT_TEST(testResizeSideHandles);
    CPPUNIT_TEST(testResizeCenterAnchor);
    CPPUNIT_TEST(testResizeRounding);
    CPPUNIT_TEST(testResizeFixedAxisSuppressed);
    CPPUNIT_TEST(testResizeDegenerateDimensions);
    CPPUNIT_TEST(testResizeEmptyCandidates);
    CPPUNIT_TEST(testResizeExactSizeFloat);
    CPPUNIT_TEST(testResizeToleranceBoundary);
    CPPUNIT_TEST_SUITE_END();

private:
    void testMoveEdgeMatch();
    void testMoveCenterMatch();
    void testMoveNearestCandidateWins();
    void testMoveToleranceBoundary();
    void testMoveToleranceMiss();
    void testMoveNegativeCoordinates();
    void testMoveIndependentAxes();
    void testMoveAlreadyAligned();
    void testMoveAppliedDeltaInvariant();
    void testMoveTieBreakStable();
    void testMoveRounding();
    void testMoveEmptyCandidates();
    void testResizeSizeMatch();
    void testResizeEdgeMatch();
    void testResizeCrossFeature();
    void testResizeSideHandles();
    void testResizeCenterAnchor();
    void testResizeRounding();
    void testResizeFixedAxisSuppressed();
    void testResizeDegenerateDimensions();
    void testResizeEmptyCandidates();
    void testResizeExactSizeFloat();
    void testResizeToleranceBoundary();
};

CPPUNIT_TEST_SUITE_REGISTRATION(SmartGuideTest);

void SmartGuideTest::testMoveEdgeMatch()
{
    const tools::Rectangle aMoving(100, 0, 150, 40);
    const std::vector<tools::Rectangle> aCandidates { rect(104, 100, 134, 130) };

    const SmartGuideMatch aMatch(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 10.0));

    CPPUNIT_ASSERT(aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Edge, aMatch.eKind);
    CPPUNIT_ASSERT_EQUAL(sal_uInt8(0), aMatch.nFeature); // moving left edge
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), aMatch.nCandidate);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(4.0, aMatch.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(104.0, aMatch.nRefPos, 1e-9);
    // the guide spans the compared shapes, not the page
    CPPUNIT_ASSERT_EQUAL(std::size_t(1), aMatch.aSegments.size());
    const SmartGuideSegment& rSeg = aMatch.aSegments[0];
    CPPUNIT_ASSERT_DOUBLES_EQUAL(104.0, rSeg.x1, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(104.0, rSeg.x2, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, rSeg.y1, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(130.0, rSeg.y2, 1e-9);
}

void SmartGuideTest::testMoveCenterMatch()
{
    // isolated geometry: the only candidate feature within tolerance is
    // the center (edges at 80 and 176 are more than 20 away)
    const tools::Rectangle aMoving(100, 0, 150, 40); // center x = 125
    const std::vector<tools::Rectangle> aCandidates { rect(80, 0, 176, 30) }; // center x = 128

    const SmartGuideMatch aMatch(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 10.0));

    CPPUNIT_ASSERT(aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Center, aMatch.eKind);
    CPPUNIT_ASSERT_EQUAL(sal_uInt8(1), aMatch.nFeature);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(3.0, aMatch.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(128.0, aMatch.nRefPos, 1e-9);
}

void SmartGuideTest::testMoveNearestCandidateWins()
{
    const tools::Rectangle aMoving(100, 0, 150, 40);
    // candidate A (first): best feature distance 9
    // candidate B (second): best feature distance 4
    const std::vector<tools::Rectangle> aCandidates {
        rect(109, 100, 169, 130),
        rect(104, 200, 134, 230)
    };

    const SmartGuideMatch aMatch(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 15.0));

    CPPUNIT_ASSERT(aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Edge, aMatch.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(4.0, aMatch.nDelta, 1e-9); // nearest wins, not the first
    CPPUNIT_ASSERT_DOUBLES_EQUAL(104.0, aMatch.nRefPos, 1e-9);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), aMatch.nCandidate);
}

void SmartGuideTest::testMoveToleranceBoundary()
{
    const tools::Rectangle aMoving(100, 0, 150, 40);
    // only the left edge at 110 is within tolerance (center 140 and
    // right edge 170 are 15 and 20 away); the boundary is inclusive
    const std::vector<tools::Rectangle> aCandidates { rect(110, 100, 170, 130) };

    const SmartGuideMatch aMatch(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 10.0));

    CPPUNIT_ASSERT(aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Edge, aMatch.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(10.0, aMatch.nDelta, 1e-9);
}

void SmartGuideTest::testMoveToleranceMiss()
{
    const tools::Rectangle aMoving(100, 0, 150, 40);
    // all candidate features (165, 190, 215) are more than 10 away from
    // every moving feature
    const std::vector<tools::Rectangle> aCandidates { rect(165, 100, 215, 130) };

    const SmartGuideMatch aMatch(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 10.0));

    CPPUNIT_ASSERT(!aMatch.bValid);
    CPPUNIT_ASSERT(aMatch.aSegments.empty());
}

void SmartGuideTest::testMoveNegativeCoordinates()
{
    const tools::Rectangle aMoving(-250, -100, -200, -60); // center x = -225, center y = -80
    const std::vector<tools::Rectangle> aCandidates { rect(-182, 0, -142, 30) };

    const SmartGuideMatch aMatchX(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 10.0));
    const SmartGuideMatch aMatchY(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, false, 10.0));

    // all candidate features are at least 18 units away on the x axis
    // and at least 90 units away on the y axis
    CPPUNIT_ASSERT(!aMatchX.bValid);
    CPPUNIT_ASSERT(!aMatchY.bValid);

    // an exact edge match at negative coordinates
    const std::vector<tools::Rectangle> aCandidates2 { rect(-250, 5, -210, 35) };
    const SmartGuideMatch aMatch2(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates2, true, 10.0));
    CPPUNIT_ASSERT(aMatch2.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Edge, aMatch2.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, aMatch2.nDelta, 1e-9);
}

void SmartGuideTest::testMoveIndependentAxes()
{
    const tools::Rectangle aMoving(100, 100, 150, 150);
    // X: left edge 104 -> delta 4; Y: no candidate within tolerance on the y axis
    const std::vector<tools::Rectangle> aCandidates { rect(104, 300, 134, 330) };

    const SmartGuideMatch aMatchX(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 10.0));
    const SmartGuideMatch aMatchY(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, false, 10.0));

    CPPUNIT_ASSERT(aMatchX.bValid);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(4.0, aMatchX.nDelta, 1e-9);
    CPPUNIT_ASSERT(!aMatchY.bValid);

    // both axes matching at once
    const std::vector<tools::Rectangle> aCandidates2 { rect(104, 103, 134, 133) };
    const SmartGuideMatch aMatchX2(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates2, true, 10.0));
    const SmartGuideMatch aMatchY2(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates2, false, 10.0));
    CPPUNIT_ASSERT(aMatchX2.bValid);
    CPPUNIT_ASSERT(aMatchY2.bValid);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(4.0, aMatchX2.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(3.0, aMatchY2.nDelta, 1e-9);
}

void SmartGuideTest::testMoveAlreadyAligned()
{
    const tools::Rectangle aMoving(100, 0, 150, 40);
    const std::vector<tools::Rectangle> aCandidates { rect(100, 100, 130, 130) };

    const SmartGuideMatch aMatch(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 10.0));

    // already aligned: the guide is still true, displacement is zero
    CPPUNIT_ASSERT(aMatch.bValid);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, aMatch.nDelta, 1e-9);
}

void SmartGuideTest::testMoveAppliedDeltaInvariant()
{
    // after applying nDelta the matched feature of the moved rectangle must
    // equal nRefPos exactly (no approximate match)
    const tools::Rectangle aMoving(100, 0, 150, 40);
    const std::vector<tools::Rectangle> aCandidates {
        rect(104, 100, 134, 130),
        rect(90, 100, 120, 130)
    };

    const SmartGuideMatch aMatch(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 10.0));
    CPPUNIT_ASSERT(aMatch.bValid);

    const double aMoved[3] {
        double(aMoving.Left()) + aMatch.nDelta,
        (double(aMoving.Left()) + double(aMoving.Right())) / 2.0 + aMatch.nDelta,
        double(aMoving.Right()) + aMatch.nDelta
    };
    CPPUNIT_ASSERT_DOUBLES_EQUAL(aMatch.nRefPos, aMoved[aMatch.nFeature], 1e-9);
}

void SmartGuideTest::testMoveTieBreakStable()
{
    const tools::Rectangle aMoving(100, 0, 150, 40);
    // both candidates have an edge exactly 4 away and the same
    // perpendicular distance (y span [100,130]); the stable geometry
    // (smaller reference position 96) decides, independent of order
    const tools::Rectangle aLeft(96, 100, 142, 130);   // edges 96 (4), 142 (8), center 119 (6)
    const tools::Rectangle aRight(104, 100, 160, 130); // edges 104 (4), 160 (10), center 132 (7)

    {
        const std::vector<tools::Rectangle> aC1 { aLeft, aRight };
        const std::vector<tools::Rectangle> aC2 { aRight, aLeft };
        const SmartGuideMatch aM1(SdrSmartGuide::findMoveAxisMatch(aMoving, aC1, true, 15.0));
        const SmartGuideMatch aM2(SdrSmartGuide::findMoveAxisMatch(aMoving, aC2, true, 15.0));
        CPPUNIT_ASSERT(aM1.bValid);
        CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Edge, aM1.eKind);
        CPPUNIT_ASSERT_DOUBLES_EQUAL(96.0, aM1.nRefPos, 1e-9);
        CPPUNIT_ASSERT_EQUAL(aM1.nRefPos, aM2.nRefPos);
        CPPUNIT_ASSERT_EQUAL(aM1.nDelta, aM2.nDelta);
        // the same geometry wins in both orderings
        CPPUNIT_ASSERT_EQUAL((aM1.nCandidate == 0), (aM2.nCandidate == 1));
    }

    // perpendicular distance decides: the candidate with the closer span
    // (gap 5) wins over the smaller reference position (gap 60)
    const tools::Rectangle aNear(104, 45, 160, 75);
    const std::vector<tools::Rectangle> aC3 { aNear, aLeft };
    const std::vector<tools::Rectangle> aC4 { aLeft, aNear };
    const SmartGuideMatch aM3(SdrSmartGuide::findMoveAxisMatch(aMoving, aC3, true, 15.0));
    const SmartGuideMatch aM4(SdrSmartGuide::findMoveAxisMatch(aMoving, aC4, true, 15.0));
    CPPUNIT_ASSERT(aM3.bValid);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(104.0, aM3.nRefPos, 1e-9);
    CPPUNIT_ASSERT_EQUAL(aM3.nRefPos, aM4.nRefPos);
    CPPUNIT_ASSERT_EQUAL(aM3.nDelta, aM4.nDelta);
    CPPUNIT_ASSERT_EQUAL((aM3.nCandidate == 0), (aM4.nCandidate == 1));
}

void SmartGuideTest::testMoveRounding()
{
    const tools::Rectangle aMoving(100, 0, 151, 40); // center x = 125.5
    // 4.5 offset to the candidate edge 130 is within tolerance but not
    // representable with integer model coordinates -> no guide
    const std::vector<tools::Rectangle> aHalf { rect(130, 100, 200, 130) };
    const SmartGuideMatch aMatch(SdrSmartGuide::findMoveAxisMatch(aMoving, aHalf, true, 10.0));
    CPPUNIT_ASSERT(!aMatch.bValid);

    // a half-integer center offset by an integral 1 is exact -> guide
    const std::vector<tools::Rectangle> aExact { rect(115, 100, 138, 130) }; // center x = 126.5
    const SmartGuideMatch aMatch2(SdrSmartGuide::findMoveAxisMatch(aMoving, aExact, true, 10.0));
    CPPUNIT_ASSERT(aMatch2.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Center, aMatch2.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(1.0, aMatch2.nDelta, 1e-9);
}

void SmartGuideTest::testMoveEmptyCandidates()
{
    const tools::Rectangle aMoving(100, 0, 150, 40);
    const std::vector<tools::Rectangle> aCandidates;

    const SmartGuideMatch aMatch(SdrSmartGuide::findMoveAxisMatch(aMoving, aCandidates, true, 10.0));
    CPPUNIT_ASSERT(!aMatch.bValid);
}

void SmartGuideTest::testResizeSizeMatch()
{
    // selection 100x50, dragging the top right handle (ref at bottom left)
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const Point aStart(100, 0);
    const Point aRef(0, 50);
    const Point aNow(130, -10); // width 130, height 60
    const std::vector<tools::Rectangle> aCandidates { rect(500, 200, 640, 280) }; // width 140

    const SmartGuideMatch aMatchX(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aCandidates, true, false, 1.2 /* cross factor */, 15.0));

    CPPUNIT_ASSERT(aMatchX.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aMatchX.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(10.0, aMatchX.nDelta, 1e-9); // 130 -> 140
    CPPUNIT_ASSERT_DOUBLES_EQUAL(140.0, aMatchX.nRefPos, 1e-9);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), aMatchX.nCandidate);
    CPPUNIT_ASSERT_EQUAL(std::size_t(2), aMatchX.aSegments.size()); // paired dimension marks
    // moving mark spans the final width at the final cross center:
    // cross extent [-10, 50] from factor 1.2 -> center 50 + (25 - 50) * 1.2 = 20
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, aMatchX.aSegments[0].x1, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(140.0, aMatchX.aSegments[0].x2, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(20.0, aMatchX.aSegments[0].y1, 1e-9);
    // reference mark spans the reference width at its cross center
    CPPUNIT_ASSERT_DOUBLES_EQUAL(500.0, aMatchX.aSegments[1].x1, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(640.0, aMatchX.aSegments[1].x2, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(240.0, aMatchX.aSegments[1].y1, 1e-9);
}

void SmartGuideTest::testResizeEdgeMatch()
{
    // selection 100x50 at (0,0), corner handle, width 130 by the mouse position
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const Point aStart(100, 0);
    const Point aRef(0, 50);
    const Point aNow(130, -10);
    const std::vector<tools::Rectangle> aCandidates { rect(54, 400, 134, 480) }; // right edge 134

    const SmartGuideMatch aMatch(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aCandidates, true, false, 1.2, 10.0));

    CPPUNIT_ASSERT(aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Edge, aMatch.eKind);
    CPPUNIT_ASSERT_EQUAL(sal_uInt8(2), aMatch.nFeature); // moving right edge
    CPPUNIT_ASSERT_DOUBLES_EQUAL(4.0, aMatch.nDelta, 1e-9); // right edge 130 -> 134
    CPPUNIT_ASSERT_DOUBLES_EQUAL(134.0, aMatch.nRefPos, 1e-9);
}

void SmartGuideTest::testResizeCrossFeature()
{
    // the moving right edge snaps to the candidate's left edge
    // (cross-feature pair: moving feature 2 vs candidate feature 0)
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const Point aStart(100, 0);
    const Point aRef(0, 50);
    const Point aNow(130, -10);
    const std::vector<tools::Rectangle> aCandidates { rect(134, 400, 204, 440) }; // left edge 134

    const SmartGuideMatch aMatch(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aCandidates, true, false, 1.2, 15.0));

    CPPUNIT_ASSERT(aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Edge, aMatch.eKind);
    CPPUNIT_ASSERT_EQUAL(sal_uInt8(2), aMatch.nFeature);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(4.0, aMatch.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(134.0, aMatch.nRefPos, 1e-9);
}

void SmartGuideTest::testResizeSideHandles()
{
    // all four side handles: equal-size snapping engages with the
    // correct sign (nMul / nDiv), including a reflected drag
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const std::vector<tools::Rectangle> aWideX { rect(500, 0, 640, 30) };   // width 140
    const std::vector<tools::Rectangle> aWideY { rect(0, 500, 30, 580) };  // height 80

    // right side handle: width 120 -> 140
    const SmartGuideMatch aRight(SdrSmartGuide::findResizeAxisMatch(
        aOrig, Point(100, 25), Point(0, 25), Point(120, 25), aWideX, true, false, 1.0, 30.0));
    CPPUNIT_ASSERT(aRight.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aRight.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(20.0, aRight.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(140.0, aRight.nRefPos, 1e-9);

    // left side handle (negative nMul and nDiv -> positive factor):
    // width 20 -> 140
    const SmartGuideMatch aLeft(SdrSmartGuide::findResizeAxisMatch(
        aOrig, Point(0, 25), Point(100, 25), Point(80, 25), aWideX, true, false, 1.0, 150.0));
    CPPUNIT_ASSERT(aLeft.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aLeft.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-120.0, aLeft.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(140.0, aLeft.nRefPos, 1e-9);

    // reflected left drag (nMul and nDiv have opposite signs -> negative
    // factor): the handle is right of the reference, so the snapped
    // shape stays reflected (handle 240, width 140)
    const SmartGuideMatch aReflected(SdrSmartGuide::findResizeAxisMatch(
        aOrig, Point(0, 25), Point(100, 25), Point(120, 25), aWideX, true, false, 1.0, 150.0));
    CPPUNIT_ASSERT(aReflected.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aReflected.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(120.0, aReflected.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(140.0, aReflected.nRefPos, 1e-9);

    // top side handle: height 30 -> 80
    const SmartGuideMatch aTop(SdrSmartGuide::findResizeAxisMatch(
        aOrig, Point(50, 0), Point(50, 50), Point(50, 20), aWideY, false, false, 1.0, 60.0));
    CPPUNIT_ASSERT(aTop.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aTop.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-50.0, aTop.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(80.0, aTop.nRefPos, 1e-9);

    // bottom side handle: height 20 -> 80
    const SmartGuideMatch aBottom(SdrSmartGuide::findResizeAxisMatch(
        aOrig, Point(50, 50), Point(50, 0), Point(50, 30), aWideY, false, false, 1.0, 60.0));
    CPPUNIT_ASSERT(aBottom.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aBottom.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(50.0, aBottom.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(80.0, aBottom.nRefPos, 1e-9);
}

void SmartGuideTest::testResizeCenterAnchor()
{
    // center based resize: ref at the center, handle at the right edge.
    // The center stays at the ref point, so the center feature is fixed
    // and only edge/size matches can apply.
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const Point aStart(100, 25);
    const Point aRef(50, 25);
    const Point aNow(135, 25); // factor 1.7 -> width 170
    const std::vector<tools::Rectangle> aCandidates { rect(125, 400, 165, 450) }; // width 40

    const SmartGuideMatch aMatch(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aCandidates, true, false, 1.0, 60.0));

    // the edge delta (-10) is closer than the center delta (10) and the
    // size delta (65, out of tolerance), so the edge match wins:
    // right edge 135 -> 125
    CPPUNIT_ASSERT(aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Edge, aMatch.eKind);
    CPPUNIT_ASSERT_EQUAL(sal_uInt8(2), aMatch.nFeature);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-10.0, aMatch.nDelta, 1e-9);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(125.0, aMatch.nRefPos, 1e-9);
}

void SmartGuideTest::testResizeRounding()
{
    // center based resize: the equal-size target would place the handle
    // on a half-integer position (170.5), which is not representable
    // with integer model coordinates -> no size guide
    const tools::Rectangle aOrig(0, 0, 200, 50);
    const Point aStart(200, 25);
    const Point aRef(100, 25);
    const Point aNow(171, 25);
    const std::vector<tools::Rectangle> aHalf { rect(500, 100, 641, 130) }; // width 141
    const SmartGuideMatch aMatch(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aHalf, true, false, 1.0, 10.0));
    CPPUNIT_ASSERT(!aMatch.bValid);

    // width 140 -> final handle 170: exact -> size guide
    const std::vector<tools::Rectangle> aExact { rect(500, 100, 640, 130) }; // width 140
    const SmartGuideMatch aMatch2(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aExact, true, false, 1.0, 10.0));
    CPPUNIT_ASSERT(aMatch2.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aMatch2.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-1.0, aMatch2.nDelta, 1e-9);
}

void SmartGuideTest::testResizeFixedAxisSuppressed()
{
    // side handle (top edge): the x dimension is fixed, no x matches
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const Point aStart(50, 0);
    const Point aRef(50, 50);
    const Point aNow(50, -10);
    const std::vector<tools::Rectangle> aCandidates { rect(10, 100, 90, 180) }; // height 80

    const SmartGuideMatch aMatchX(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aCandidates, true, true /* fixed */, 1.0, 25.0));
    CPPUNIT_ASSERT(!aMatchX.bValid);

    // the free (y) axis still matches: height 60 -> 80
    const SmartGuideMatch aMatchY(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aCandidates, false, false, 1.0, 25.0));
    CPPUNIT_ASSERT(aMatchY.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aMatchY.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(-20.0, aMatchY.nDelta, 1e-9); // 60 -> 80 moves the handle up by 20
}

void SmartGuideTest::testResizeDegenerateDimensions()
{
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const Point aStart(100, 0);
    const Point aRef(0, 50);

    // zero width candidate (a line): no size match, edge match still works
    const std::vector<tools::Rectangle> aLine { rect(130, 0, 130, 40) };
    const Point aNow(130, -10);
    SmartGuideMatch aMatch(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aLine, true, false, 1.2, 10.0));
    CPPUNIT_ASSERT(aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Edge, aMatch.eKind); // not a Size match

    // zero current dimension (handle above the ref): no size match
    const Point aNowZero(0, 50);
    const std::vector<tools::Rectangle> aWide { rect(500, 200, 640, 280) };
    aMatch = SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNowZero, aWide, true, false, 1.0, 10.0);
    CPPUNIT_ASSERT(!aMatch.bValid);

    // degenerate anchor (start above ref)
    const Point aStartDeg(0, 50);
    aMatch = SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStartDeg, aRef, aNow, aWide, true, false, 1.0, 10.0);
    CPPUNIT_ASSERT(!aMatch.bValid);
}

void SmartGuideTest::testResizeEmptyCandidates()
{
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const Point aStart(100, 0);
    const Point aRef(0, 50);
    const Point aNow(130, -10);
    const std::vector<tools::Rectangle> aCandidates;

    const SmartGuideMatch aMatch(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aCandidates, true, false, 1.2, 10.0));
    CPPUNIT_ASSERT(!aMatch.bValid);
}

void SmartGuideTest::testResizeExactSizeFloat()
{
    // review regression: original width 100, right handle at 110,
    // reference width 110, tolerance 0; 1.1 * 100 produces
    // 110.00000000000001, so the desired handle coordinate must be
    // normalized to the exact integer value before the tolerance
    // comparison
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const Point aStart(100, 0);
    const Point aRef(0, 50);
    const Point aNow(110, 0);
    const std::vector<tools::Rectangle> aCandidates { rect(500, 300, 610, 340) }; // width 110

    const SmartGuideMatch aMatch(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aCandidates, true, false, 1.0, 0.0));

    CPPUNIT_ASSERT_MESSAGE("110-wide shape must retain exact equal-width guide", aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aMatch.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(0.0, aMatch.nDelta, 0.0);
}

void SmartGuideTest::testResizeToleranceBoundary()
{
    // review regression: a target exactly at the inclusive tolerance
    // boundary must match (float normalization, see above)
    const tools::Rectangle aOrig(0, 0, 100, 50);
    const Point aStart(100, 0);
    const Point aRef(0, 50);
    const Point aNow(100, 0);
    const std::vector<tools::Rectangle> aCandidates { rect(500, 300, 610, 340) }; // width 110

    const SmartGuideMatch aMatch(SdrSmartGuide::findResizeAxisMatch(
        aOrig, aStart, aRef, aNow, aCandidates, true, false, 1.0, 10.0));

    CPPUNIT_ASSERT_MESSAGE("110-wide target is exactly at the inclusive tolerance boundary", aMatch.bValid);
    CPPUNIT_ASSERT_EQUAL(SmartGuideKind::Size, aMatch.eKind);
    CPPUNIT_ASSERT_DOUBLES_EQUAL(10.0, aMatch.nDelta, 0.0);
}

} // end of anonymous namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
