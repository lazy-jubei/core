/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <test/bootstrapfixture.hxx>
#include <sfx2/app.hxx>

#include <rtl/ref.hxx>

#include <vcl/virdev.hxx>
#include <svx/svdmodel.hxx>
#include <svx/svdpage.hxx>
#include <svx/svdobj.hxx>
#include <svx/svdorect.hxx>
#include <svx/svdview.hxx>
#include <svx/svddrgv.hxx>
#include <svx/svddrgmt.hxx>
#include <svx/svdmrkv.hxx>
#include <svx/svdsnpv.hxx>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

namespace
{
/// Integration tests for the smart guide drag integration
/// (SdrDragMethod / SdrDragMove / SdrDragResize): the full
/// SdrView drag pipeline with real marking, snapping, constraints
/// and the final-geometry guide validation
class SmartGuideDragTest : public test::BootstrapFixture
{
public:
    // These in-memory drawing tests do not need content providers.
    SmartGuideDragTest()
        : test::BootstrapFixture(true, false)
    {
    }

    void setUp() override
    {
        test::BootstrapFixture::setUp();
        // SdrModel constructs an EditEngine which listens to SfxApplication.
        SfxApplication::GetOrCreate();
    }

private:
    struct Env
    {
        std::unique_ptr<SdrModel> pModel;
        rtl::Reference<SdrPage> pPage;
        VclPtr<VirtualDevice> pDevice;
        std::unique_ptr<SdrView> pView;

        ~Env()
        {
            // tear down the view before the output device it
            // points at, then dispose the device explicitly
            pView.reset();
            pDevice.disposeAndClear();
        }
    };

    void makeEnv(Env& rEnv)
    {
        rEnv.pModel.reset(new SdrModel(nullptr, nullptr, true));
        rEnv.pPage = new SdrPage(*rEnv.pModel, false);
        rEnv.pPage->SetSize(Size(4000, 4000));
        rEnv.pModel->InsertPage(rEnv.pPage.get(), 0);
        rEnv.pDevice = new VirtualDevice();
        rEnv.pDevice->SetOutputSize(Size(4000, 4000));
        rEnv.pView.reset(new SdrView(*rEnv.pModel, rEnv.pDevice));
        // note: hideMarkHandles() must NOT be used here - it makes
        // SetMarkHandles() return early, leaving maHdlList empty and
        // mpMarkedPV null, which breaks the drag handle lookup and
        // the smart guide candidate collection (GetDragPV())
        rEnv.pView->ShowSdrPage(rEnv.pPage.get());
    }

    /// Pin every snap-related setting explicitly so the shared
    /// SdrView defaults (grid, border, help, object snapping and
    /// the minimum movement threshold) cannot mask the smart
    /// guide matches: only the smart guides or the raw mouse
    /// position may decide the geometry. The magnetic distance is
    /// pinned in pixels as well because SdrView construction and
    /// BegDragObj re-recalculate it through SetActualWin().
    void makeFixtureDeterministic(Env& rEnv)
    {
        SdrView& rView = *rEnv.pView;
        // Match Draw's FrameView default and exercise SdrDragResize,
        // rather than the object's special-handle drag implementation.
        rView.SetFrameDragSingles();
        rView.SetSnapEnabled(true);
        rView.SetGridSnap(false);
        rView.SetBordSnap(false);
        rView.SetHlplSnap(false);
        rView.SetOFrmSnap(false);
        rView.SetOPntSnap(false);
        rView.SetOConSnap(false);
        rView.SetSnapMagneticPixel(8);
        rView.SetSnapMagnetic(Size(8, 8));
        // the 4 unit programmatic mouse moves must not be
        // swallowed by the minimum movement threshold
        rView.SetMinMoveDistancePixel(1);
    }

    rtl::Reference<SdrRectObj> addRect(Env& rEnv, const tools::Rectangle& rRect)
    {
        auto pRect(new SdrRectObj(*rEnv.pModel, rRect));
        rEnv.pPage->NbcInsertObject(pRect);
        return pRect;
    }

public:
    CPPUNIT_TEST_SUITE(SmartGuideDragTest);
    CPPUNIT_TEST(testMoveCenterGuide);
    CPPUNIT_TEST(testMoveCenterFeatureOff);
    CPPUNIT_TEST(testMoveCenterSuppressed);
    CPPUNIT_TEST(testMoveCenterSnapDisabled);
    CPPUNIT_TEST(testMoveSnapDisabledAtStart);
    CPPUNIT_TEST(testMoveExcludesMarkedFromReferences);
    CPPUNIT_TEST(testMoveDragCancelled);
    CPPUNIT_TEST(testResizeEqualSize);
    CPPUNIT_TEST(testResizeEqualSizeFeatureOff);
    CPPUNIT_TEST(testResizeAspectRatioConstraint);
    CPPUNIT_TEST(testResizeEqualSizeBothAxes);
    CPPUNIT_TEST_SUITE_END();

private:
    void testMoveCenterGuide();
    void testMoveCenterFeatureOff();
    void testMoveCenterSuppressed();
    void testMoveCenterSnapDisabled();
    void testMoveSnapDisabledAtStart();
    void testMoveExcludesMarkedFromReferences();
    void testMoveDragCancelled();
    void testResizeEqualSize();
    void testResizeEqualSizeFeatureOff();
    void testResizeAspectRatioConstraint();
    void testResizeEqualSizeBothAxes();
};

CPPUNIT_TEST_SUITE_REGISTRATION(SmartGuideDragTest);

// the shared move geometry: the candidate's center x is 150, its
// edges (100/200) are far from the mover's corners, so only the
// center match (a smart guide, the existing corner snapping does
// not snap centers) can engage
void SmartGuideDragTest::testMoveCenterGuide()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(100, 300), Point(200, 350)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(58, 300), Point(258, 350)));

    SdrView& rView = *aEnv.pView;
    rView.SetSmartGuidesEnabled(true);
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    // mover center 158; the mouse moves 4 (within the magnetic 8
    // and above the minimum movement) to 154, the center guide
    // (target 150, displacement 4) wins over the non-snapping
    // corners
    CPPUNIT_ASSERT(rView.BegDragObj(Point(158, 325), aEnv.pDevice, nullptr));
    rView.MovDragObj(Point(154, 325));
    rView.EndDragObj(false);

    CPPUNIT_ASSERT_EQUAL(tools::Long(50), pMover->GetSnapRect().Left());
}

// with the feature off (the shared default) the plain mouse
// position applies: center 151, left edge 54
void SmartGuideDragTest::testMoveCenterFeatureOff()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(100, 300), Point(200, 350)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(58, 300), Point(258, 350)));

    SdrView& rView = *aEnv.pView;
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    CPPUNIT_ASSERT(rView.BegDragObj(Point(158, 325), aEnv.pDevice, nullptr));
    rView.MovDragObj(Point(154, 325));
    rView.EndDragObj(false);

    CPPUNIT_ASSERT_EQUAL(tools::Long(54), pMover->GetSnapRect().Left());
}

// a temporarily suppressed feature (the application snap modifier)
// must disable the attraction although the feature itself stays on
void SmartGuideDragTest::testMoveCenterSuppressed()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(100, 300), Point(200, 350)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(58, 300), Point(258, 350)));

    SdrView& rView = *aEnv.pView;
    rView.SetSmartGuidesEnabled(true);
    rView.SetSmartGuidesSuppressed(true);
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    CPPUNIT_ASSERT(rView.BegDragObj(Point(158, 325), aEnv.pDevice, nullptr));
    rView.MovDragObj(Point(154, 325));
    rView.EndDragObj(false);

    CPPUNIT_ASSERT_EQUAL(tools::Long(54), pMover->GetSnapRect().Left());
}

// snapping disabled: no corner snapping and no smart guides
void SmartGuideDragTest::testMoveCenterSnapDisabled()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(100, 300), Point(200, 350)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(58, 300), Point(258, 350)));

    SdrView& rView = *aEnv.pView;
    rView.SetSmartGuidesEnabled(true);
    rView.SetSnapEnabled(false);
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    CPPUNIT_ASSERT(rView.BegDragObj(Point(158, 325), aEnv.pDevice, nullptr));
    rView.MovDragObj(Point(154, 325));
    rView.EndDragObj(false);

    CPPUNIT_ASSERT_EQUAL(tools::Long(54), pMover->GetSnapRect().Left());
}

// a temporarily disabled snapping (e.g. Ctrl held at drag start) must
// not block the candidate collection; releasing the modifier during
// the same drag must still allow the guide to engage
void SmartGuideDragTest::testMoveSnapDisabledAtStart()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(100, 300), Point(200, 350)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(58, 300), Point(258, 350)));

    SdrView& rView = *aEnv.pView;
    rView.SetSmartGuidesEnabled(true);
    rView.SetSnapEnabled(false);
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    CPPUNIT_ASSERT(rView.BegDragObj(Point(158, 325), aEnv.pDevice, nullptr));
    rView.SetSnapEnabled(true);
    rView.MovDragObj(Point(154, 325));
    rView.EndDragObj(false);

    CPPUNIT_ASSERT_EQUAL(tools::Long(50), pMover->GetSnapRect().Left());
}

// a marked (but not dragged) object must not be a reference: its
// center (150, displacement 0) is closer than the unmarked
// candidate's center (152, displacement 2); the reference must win
void SmartGuideDragTest::testMoveExcludesMarkedFromReferences()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(102, 300), Point(202, 350)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(54, 300), Point(254, 350)));
    rtl::Reference<SdrRectObj> pMarked =
        addRect(aEnv, tools::Rectangle(Point(134, 500), Point(166, 550)));

    SdrView& rView = *aEnv.pView;
    rView.SetSmartGuidesEnabled(true);
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());
    rView.MarkObj(pMarked.get(), rView.GetSdrPageView());

    // the marked selection (union) center x is 154; the mouse moves
    // 4 to 150
    CPPUNIT_ASSERT(rView.BegDragObj(Point(154, 325), aEnv.pDevice, nullptr));
    rView.MovDragObj(Point(150, 325));
    rView.EndDragObj(false);

    // pMarked (center 150, displacement 0) must not be a reference;
    // the actual reference is pCandidate (center 152, displacement 2)
    CPPUNIT_ASSERT_EQUAL(tools::Long(52), pMover->GetSnapRect().Left());
}

// a cancelled drag must not apply the snapped position
void SmartGuideDragTest::testMoveDragCancelled()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(100, 300), Point(200, 350)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(58, 300), Point(258, 350)));

    SdrView& rView = *aEnv.pView;
    rView.SetSmartGuidesEnabled(true);
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    CPPUNIT_ASSERT(rView.BegDragObj(Point(158, 325), aEnv.pDevice, nullptr));
    rView.MovDragObj(Point(154, 325));
    rView.BrkDragObj();

    CPPUNIT_ASSERT_EQUAL(tools::Long(58), pMover->GetSnapRect().Left());
}

// equal size (opposite-edge anchor, the shared default): the right
// handle of a 100 wide shape is dragged to 134 (displacement 6);
// the 140 wide candidate engages
void SmartGuideDragTest::testResizeEqualSize()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(500, 300), Point(640, 380)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(0, 0), Point(100, 50)));

    SdrView& rView = *aEnv.pView;
    rView.SetSmartGuidesEnabled(true);
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    SdrHdl* pHdl = rView.GetHdlList().GetHdl(SdrHdlKind::Right);
    CPPUNIT_ASSERT(pHdl != nullptr);
    CPPUNIT_ASSERT(rView.BegDragObj(pHdl->GetPos(), aEnv.pDevice, pHdl));
    CPPUNIT_ASSERT(dynamic_cast<SdrDragResize*>(rView.GetDragMethod()) != nullptr);
    rView.MovDragObj(Point(134, 25));
    rView.EndDragObj(false);

    const tools::Rectangle& rSnap = pMover->GetSnapRect();
    CPPUNIT_ASSERT_EQUAL(tools::Long(140), rSnap.Right() - rSnap.Left());
    CPPUNIT_ASSERT_EQUAL(tools::Long(50), rSnap.Bottom() - rSnap.Top());
}

// with the feature off the raw handle position applies (width 134)
void SmartGuideDragTest::testResizeEqualSizeFeatureOff()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(500, 300), Point(640, 380)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(0, 0), Point(100, 50)));

    SdrView& rView = *aEnv.pView;
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    SdrHdl* pHdl = rView.GetHdlList().GetHdl(SdrHdlKind::Right);
    CPPUNIT_ASSERT(pHdl != nullptr);
    CPPUNIT_ASSERT(rView.BegDragObj(pHdl->GetPos(), aEnv.pDevice, pHdl));
    CPPUNIT_ASSERT(dynamic_cast<SdrDragResize*>(rView.GetDragMethod()) != nullptr);
    rView.MovDragObj(Point(134, 25));
    rView.EndDragObj(false);

    const tools::Rectangle& rSnap = pMover->GetSnapRect();
    CPPUNIT_ASSERT_EQUAL(tools::Long(134), rSnap.Right() - rSnap.Left());
    CPPUNIT_ASSERT_EQUAL(tools::Long(50), rSnap.Bottom() - rSnap.Top());
}

// aspect ratio (Shift) constraint: only the x size match is within
// the magnetic (134 vs 140, displacement 6; 67 vs 80 is out of
// range); the factor equalization keeps the raw ratio 2.0, so the
// shape ends at exactly 140x70 and no y match may be shown
void SmartGuideDragTest::testResizeAspectRatioConstraint()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(500, 300), Point(640, 380)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(0, 0), Point(100, 50)));

    SdrView& rView = *aEnv.pView;
    rView.SetSmartGuidesEnabled(true);
    rView.SetOrtho(true);
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    SdrHdl* pHdl = rView.GetHdlList().GetHdl(SdrHdlKind::LowerRight);
    CPPUNIT_ASSERT(pHdl != nullptr);
    CPPUNIT_ASSERT(rView.BegDragObj(pHdl->GetPos(), aEnv.pDevice, pHdl));
    CPPUNIT_ASSERT(dynamic_cast<SdrDragResize*>(rView.GetDragMethod()) != nullptr);
    rView.MovDragObj(Point(134, 67));
    rView.EndDragObj(false);

    const tools::Rectangle& rSnap = pMover->GetSnapRect();
    CPPUNIT_ASSERT_EQUAL(tools::Long(140), rSnap.Right() - rSnap.Left());
    CPPUNIT_ASSERT_EQUAL(tools::Long(70), rSnap.Bottom() - rSnap.Top());
}

// without the aspect ratio constraint both independent size matches
// apply (134 and 74 are each within the magnetic of 140 and 80):
// 140 x 80
void SmartGuideDragTest::testResizeEqualSizeBothAxes()
{
    Env aEnv;
    makeEnv(aEnv);
    makeFixtureDeterministic(aEnv);
    rtl::Reference<SdrRectObj> pCandidate =
        addRect(aEnv, tools::Rectangle(Point(500, 300), Point(640, 380)));
    rtl::Reference<SdrRectObj> pMover =
        addRect(aEnv, tools::Rectangle(Point(0, 0), Point(100, 50)));

    SdrView& rView = *aEnv.pView;
    rView.SetSmartGuidesEnabled(true);
    rView.MarkObj(pMover.get(), rView.GetSdrPageView());

    SdrHdl* pHdl = rView.GetHdlList().GetHdl(SdrHdlKind::LowerRight);
    CPPUNIT_ASSERT(pHdl != nullptr);
    CPPUNIT_ASSERT(rView.BegDragObj(pHdl->GetPos(), aEnv.pDevice, pHdl));
    CPPUNIT_ASSERT(dynamic_cast<SdrDragResize*>(rView.GetDragMethod()) != nullptr);
    rView.MovDragObj(Point(134, 74));
    rView.EndDragObj(false);

    const tools::Rectangle& rSnap = pMover->GetSnapRect();
    CPPUNIT_ASSERT_EQUAL(tools::Long(140), rSnap.Right() - rSnap.Left());
    CPPUNIT_ASSERT_EQUAL(tools::Long(80), rSnap.Bottom() - rSnap.Top());
}

} // end of anonymous namespace

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
