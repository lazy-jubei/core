/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <svdsmguid.hxx>

#include <cmath>
#include <cfloat>

namespace
{
    /// deterministic tie-break rank; the SmartGuideKind enumeration
    /// order is the rank (Edge < Size < Center), independent of the
    /// candidate order
    sal_uInt8 getKindRank(SmartGuideKind eKind)
    {
        switch (eKind)
        {
            case SmartGuideKind::Edge:
                return 0;
            case SmartGuideKind::Size:
                return 1;
            case SmartGuideKind::Center:
                return 2;
            default:
                return 3; // None is never a match
        }
    }

    /// model coordinates are integer: a non-integral displacement does
    /// not produce an exact alignment after rounding, so such a match
    /// must not be shown (e.g. a half-integer center offset)
    bool isIntegral(double nValue)
    {
        return std::abs(nValue - std::llround(nValue)) < 1e-9;
    }

    /// distance between two intervals on a line (0 when they overlap)
    double intervalGap(double nMin1, double nMax1, double nMin2, double nMax2)
    {
        if (nMax1 < nMin2)
            return nMin2 - nMax1;
        if (nMax2 < nMin1)
            return nMin1 - nMax2;
        return 0.0;
    }

    struct MatchCandidate
    {
        // result
        double nDelta { 0.0 };
        SmartGuideKind eKind { SmartGuideKind::None };
        double nRefPos { 0.0 };
        sal_uInt8 nFeature { 0 };
        int nCand { -1 };
        // guide geometry
        bool bIsSize { false };
        // line span perpendicular to the matched axis (min, max)
        double nSpan1 { 0.0 };
        double nSpan2 { 0.0 };
        // size matches: matched-axis extent (min, max) of the reference shape
        double nRefDim1 { 0.0 };
        double nRefDim2 { 0.0 };
        // tie-break keys, functions of the geometry only (so the result
        // does not depend on the candidate order): |delta|, kind rank,
        // perpendicular distance, matched reference position, then the
        // candidate rectangle extents
        double nAbs { DBL_MAX };
        sal_uInt8 nRank { 3 };
        double nGap { DBL_MAX };
        double nL { DBL_MAX };
        double nT { DBL_MAX };
        double nR { DBL_MAX };
        double nB { DBL_MAX };
    };

    /// returns true when rNew is strictly better than rBest
    /// (lexicographic, smaller wins)
    bool isBetter(const MatchCandidate& rNew, const MatchCandidate& rBest)
    {
        if (rNew.nAbs != rBest.nAbs)
            return rNew.nAbs < rBest.nAbs;
        if (rNew.nRank != rBest.nRank)
            return rNew.nRank < rBest.nRank;
        if (rNew.nGap != rBest.nGap)
            return rNew.nGap < rBest.nGap;
        if (rNew.nRefPos != rBest.nRefPos)
            return rNew.nRefPos < rBest.nRefPos;
        if (rNew.nL != rBest.nL)
            return rNew.nL < rBest.nL;
        if (rNew.nT != rBest.nT)
            return rNew.nT < rBest.nT;
        if (rNew.nR != rBest.nR)
            return rNew.nR < rBest.nR;
        return rNew.nB < rBest.nB;
    }

    void recordKeys(MatchCandidate& rC, double nDelta, SmartGuideKind eKind,
        double nGap, const tools::Rectangle& rRect)
    {
        rC.nAbs = std::abs(nDelta);
        rC.nRank = getKindRank(eKind);
        rC.nGap = nGap;
        rC.nL = double(rRect.Left());
        rC.nT = double(rRect.Top());
        rC.nR = double(rRect.Right());
        rC.nB = double(rRect.Bottom());
    }
}

void SdrSmartGuide::getAxisPositions(
    const tools::Rectangle& rRect,
    bool bXAxis,
    double& rEdge1,
    double& rCenter,
    double& rEdge2)
{
    if (bXAxis)
    {
        rEdge1 = double(rRect.Left());
        rCenter = (double(rRect.Left()) + double(rRect.Right())) / 2.0;
        rEdge2 = double(rRect.Right());
    }
    else
    {
        rEdge1 = double(rRect.Top());
        rCenter = (double(rRect.Top()) + double(rRect.Bottom())) / 2.0;
        rEdge2 = double(rRect.Bottom());
    }
}

SmartGuideMatch SdrSmartGuide::findMoveAxisMatch(
    const tools::Rectangle& rMoving,
    const std::vector<tools::Rectangle>& rCandidates,
    bool bXAxis,
    double fTolerance)
{
    SmartGuideMatch aMatch;
    if (fTolerance < 0.0 || rCandidates.empty())
        return aMatch;

    double aMoving[3];
    getAxisPositions(rMoving, bXAxis, aMoving[0], aMoving[1], aMoving[2]);
    const double nMvCross1(bXAxis ? double(rMoving.Top()) : double(rMoving.Left()));
    const double nMvCross2(bXAxis ? double(rMoving.Bottom()) : double(rMoving.Right()));
    const SmartGuideKind aKind[3] {
        SmartGuideKind::Edge, SmartGuideKind::Center, SmartGuideKind::Edge };
    const sal_uInt8 aFeature[3] { 0, 1, 2 };

    MatchCandidate aBest;
    bool bFound(false);

    for(size_t i = 0; i < rCandidates.size(); ++i)
    {
        const tools::Rectangle& rC(rCandidates[i]);
        double aCand[3];
        getAxisPositions(rC, bXAxis, aCand[0], aCand[1], aCand[2]);
        const double nCcCross1(bXAxis ? double(rC.Top()) : double(rC.Left()));
        const double nCcCross2(bXAxis ? double(rC.Bottom()) : double(rC.Right()));
        const double nGap(intervalGap(nMvCross1, nMvCross2, nCcCross1, nCcCross2));

        for(int c = 0; c < 3; ++c)
        {
            for(int m = 0; m < 3; ++m)
            {
                const double nDelta(aCand[c] - aMoving[m]);

                if (std::abs(nDelta) > fTolerance)
                    continue;
                // validate after rounding: the whole-shape displacement
                // must be integral to keep the alignment exact
                if (!isIntegral(nDelta))
                    continue;

                MatchCandidate aNew;
                aNew.nDelta = nDelta;
                aNew.eKind = aKind[c];
                aNew.nRefPos = aCand[c];
                aNew.nFeature = aFeature[m];
                aNew.nCand = int(i);
                aNew.nSpan1 = std::min(nMvCross1, nCcCross1);
                aNew.nSpan2 = std::max(nMvCross2, nCcCross2);
                recordKeys(aNew, nDelta, aKind[c], nGap, rC);

                if (!bFound || isBetter(aNew, aBest))
                {
                    bFound = true;
                    aBest = aNew;
                }
            }
        }
    }

    if (!bFound)
        return aMatch;

    aMatch.bValid = true;
    aMatch.eKind = aBest.eKind;
    aMatch.nDelta = aBest.nDelta;
    aMatch.nRefPos = aBest.nRefPos;
    aMatch.nFeature = aBest.nFeature;
    aMatch.nCandidate = aBest.nCand;
    SmartGuideSegment aSeg;
    if (bXAxis)
    {
        aSeg.x1 = aSeg.x2 = aBest.nRefPos;
        aSeg.y1 = aBest.nSpan1;
        aSeg.y2 = aBest.nSpan2;
    }
    else
    {
        aSeg.y1 = aSeg.y2 = aBest.nRefPos;
        aSeg.x1 = aBest.nSpan1;
        aSeg.x2 = aBest.nSpan2;
    }
    aMatch.aSegments.push_back(aSeg);
    return aMatch;
}

SmartGuideMatch SdrSmartGuide::findResizeAxisMatch(
    const tools::Rectangle& rOrig,
    const Point& aStart,
    const Point& aRef,
    const Point& aNow,
    const std::vector<tools::Rectangle>& rCandidates,
    bool bXAxis,
    bool bAxisFixed,
    double fCrossFact,
    double fTolerance)
{
    SmartGuideMatch aMatch;
    if (bAxisFixed || fTolerance < 0.0 || rCandidates.empty())
        return aMatch;

    const double nStart(bXAxis ? double(aStart.X()) : double(aStart.Y()));
    const double nRef(bXAxis ? double(aRef.X()) : double(aRef.Y()));
    const double nNow(bXAxis ? double(aNow.X()) : double(aNow.Y()));
    const double nDiv(nStart - nRef);
    const double nMul(nNow - nRef);

    // degenerate anchor (handle above the reference point)
    if (std::abs(nDiv) < 1e-9)
        return aMatch;

    const double nOrig1(bXAxis ? double(rOrig.Left()) : double(rOrig.Top()));
    const double nOrig2(bXAxis ? double(rOrig.Right()) : double(rOrig.Bottom()));
    const double nOrigW(nOrig2 - nOrig1);
    // extent perpendicular to the matched axis (original and its center)
    const double nCross1(bXAxis ? double(rOrig.Top()) : double(rOrig.Left()));
    const double nCross2(bXAxis ? double(rOrig.Bottom()) : double(rOrig.Right()));
    // the resize reference of the cross axis is the same point aRef
    const double nCrossRef(bXAxis ? double(aRef.Y()) : double(aRef.X()));
    // the cross extent of the moved shape follows fCrossFact, so the
    // guide endpoints use both scale factors
    const double nCrossFinal1(nCrossRef + (nCross1 - nCrossRef) * fCrossFact);
    const double nCrossFinal2(nCrossRef + (nCross2 - nCrossRef) * fCrossFact);
    const double nCenterCross(nCrossRef + ((nCross1 + nCross2) / 2.0 - nCrossRef) * fCrossFact);

    const double aOrigPos[3] { nOrig1, (nOrig1 + nOrig2) / 2.0, nOrig2 };
    const sal_uInt8 aFeature[3] { 0, 1, 2 };

    MatchCandidate aBest;
    bool bFound(false);

    for(size_t i = 0; i < rCandidates.size(); ++i)
    {
        const tools::Rectangle& rC(rCandidates[i]);
        const double nCand1(bXAxis ? double(rC.Left()) : double(rC.Top()));
        const double nCand2(bXAxis ? double(rC.Right()) : double(rC.Bottom()));
        const double nCandW(nCand2 - nCand1);
        const double nCCross1(bXAxis ? double(rC.Top()) : double(rC.Left()));
        const double nCCross2(bXAxis ? double(rC.Bottom()) : double(rC.Right()));
        // a negative cross factor reverses nCrossFinal1/2, so normalize
        // to (min, max) before the gap computation
        const double nGap(intervalGap(
            std::min(nCrossFinal1, nCrossFinal2),
            std::max(nCrossFinal1, nCrossFinal2),
            nCCross1, nCCross2));

        const double aCandPos[3] { nCand1, (nCand1 + nCand2) / 2.0, nCand2 };
        const SmartGuideKind aKind[3] {
            SmartGuideKind::Edge, SmartGuideKind::Center, SmartGuideKind::Edge };

        for(int m = 0; m < 3; ++m)
        {
            // the moving feature m is compared against all candidate
            // features (cross-feature pairs, e.g. dragging a right edge
            // to a candidate's left edge)
            const double nFeatToRef(aOrigPos[m] - nRef);

            if (std::abs(nFeatToRef) <= 1e-9)
                continue;

            for(int c = 0; c < 3; ++c)
            {
                // edge/center match: solve the factor from the target
                // position; the desired handle coordinate is validated
                // and normalized to the exact integer value BEFORE the
                // (inclusive) tolerance comparison, so a match sitting
                // exactly on the boundary is not lost to floating-point
                // error (e.g. 1.1 * 100 = 110.00000000000001)
                const double fFact((aCandPos[c] - nRef) / nFeatToRef);
                const double nDesired(nRef + fFact * nDiv);

                if (!isIntegral(nDesired))
                    continue;

                const double nDelta(std::llround(nDesired) - nNow);

                if (std::abs(nDelta) <= fTolerance)
                {
                    MatchCandidate aNew;
                    aNew.nDelta = nDelta;
                    aNew.eKind = aKind[c];
                    aNew.nRefPos = aCandPos[c];
                    aNew.nFeature = aFeature[m];
                    aNew.nCand = int(i);
                    aNew.nSpan1 = std::min({ nCrossFinal1, nCrossFinal2, nCCross1 });
                    aNew.nSpan2 = std::max({ nCrossFinal1, nCrossFinal2, nCCross2 });
                    recordKeys(aNew, nDelta, aKind[c], nGap, rC);

                    if (!bFound || isBetter(aNew, aBest))
                    {
                        bFound = true;
                        aBest = aNew;
                    }
                }
            }
        }

        // size match: independent of the moving feature m, only for
        // non-degenerate dimensions on both sides
        if (nMul != 0.0 && std::abs(nOrigW) >= 1.0 && std::abs(nCandW) >= 1.0)
        {
            // keep the side the handle is on (sign of nMul / nDiv,
            // including reflected drags); the desired handle coordinate
            // is normalized to the exact integer value before the
            // tolerance comparison (see the edge/center match above)
            const double fFact(
                (nMul / nDiv >= 0.0 ? 1.0 : -1.0) * std::abs(nCandW) / std::abs(nOrigW));
            const double nDesired(nRef + fFact * nDiv);

            if (isIntegral(nDesired))
            {
                const double nDelta(std::llround(nDesired) - nNow);

                if (std::abs(nDelta) <= fTolerance)
                {
                    MatchCandidate aNew;
                    aNew.nDelta = nDelta;
                    aNew.eKind = SmartGuideKind::Size;
                    aNew.nRefPos = nCandW;
                    aNew.nFeature = 3;
                    aNew.nCand = int(i);
                    aNew.bIsSize = true;
                    aNew.nRefDim1 = nCand1;
                    aNew.nRefDim2 = nCand2;
                    aNew.nSpan1 = nCCross1;
                    aNew.nSpan2 = nCCross2;
                    recordKeys(aNew, nDelta, SmartGuideKind::Size,
                        intervalGap(nCenterCross, nCenterCross, nCCross1, nCCross2), rC);

                    if (!bFound || isBetter(aNew, aBest))
                    {
                        bFound = true;
                        aBest = aNew;
                    }
                }
            }
        }
    }

    if (!bFound)
        return aMatch;

    aMatch.bValid = true;
    aMatch.eKind = aBest.eKind;
    aMatch.nDelta = aBest.nDelta;
    aMatch.nRefPos = aBest.nRefPos;
    aMatch.nFeature = aBest.nFeature;
    aMatch.nCandidate = aBest.nCand;

    // final factor implied by the match (guide geometry)
    const double fFact((nNow + aBest.nDelta - nRef) / nDiv);
    const double nFinal1(nRef + (nOrig1 - nRef) * fFact);
    const double nFinal2(nRef + (nOrig2 - nRef) * fFact);

    if (aBest.bIsSize)
    {
        // paired dimension marks: one on the resized selection, one on
        // the reference shape, each spanning the matched dimension
        SmartGuideSegment aMovingMark;
        SmartGuideSegment aRefMark;
        if (bXAxis)
        {
            aMovingMark.x1 = nFinal1;
            aMovingMark.x2 = nFinal2;
            aMovingMark.y1 = aMovingMark.y2 = nCenterCross;
            aRefMark.x1 = aBest.nRefDim1;
            aRefMark.x2 = aBest.nRefDim2;
            aRefMark.y1 = aRefMark.y2 = (aBest.nSpan1 + aBest.nSpan2) / 2.0;
        }
        else
        {
            aMovingMark.y1 = nFinal1;
            aMovingMark.y2 = nFinal2;
            aMovingMark.x1 = aMovingMark.x2 = nCenterCross;
            aRefMark.y1 = aBest.nRefDim1;
            aRefMark.y2 = aBest.nRefDim2;
            aRefMark.x1 = aRefMark.x2 = (aBest.nSpan1 + aBest.nSpan2) / 2.0;
        }
        aMatch.aSegments.push_back(aMovingMark);
        aMatch.aSegments.push_back(aRefMark);
    }
    else
    {
        SmartGuideSegment aSeg;
        if (bXAxis)
        {
            aSeg.x1 = aSeg.x2 = aBest.nRefPos;
            aSeg.y1 = aBest.nSpan1;
            aSeg.y2 = aBest.nSpan2;
        }
        else
        {
            aSeg.y1 = aSeg.y2 = aBest.nRefPos;
            aSeg.x1 = aBest.nSpan1;
            aSeg.x2 = aBest.nSpan2;
        }
        aMatch.aSegments.push_back(aSeg);
    }

    return aMatch;
}
