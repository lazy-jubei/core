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

#pragma once

#include <svx/svxdllapi.h>
#include <tools/gen.hxx>
#include <vector>

/// Kind of a smart guide (dynamic alignment / equal size) match.
/// The enumerator order (Edge < Size < Center) is the deterministic
/// tie-break rank.
enum class SmartGuideKind
{
    /// no match
    None,
    /// an edge of the moving shape matches an edge of the reference shape
    Edge,
    /// the width (or height) of the moving shape matches the one of the reference shape
    Size,
    /// the center of the moving shape matches the center of the reference shape
    Center
};

/// One line segment of the transient guide overlay (model units)
struct SmartGuideSegment
{
    double x1 { 0.0 };
    double y1 { 0.0 };
    double x2 { 0.0 };
    double y2 { 0.0 };
};

/// Result of searching for an alignment/size match on one axis
struct SVXCORE_DLLPUBLIC SmartGuideMatch
{
    /// true when a match was found within the tolerance
    bool bValid { false };
    /// kind of the match
    SmartGuideKind eKind { SmartGuideKind::None };
    /// displacement to apply on the searched axis (model units);
    /// always integral, see the class documentation
    double nDelta { 0.0 };
    /// position of the matched feature on the reference shape
    /// (the reference dimension for Size matches)
    double nRefPos { 0.0 };
    /// index of the matched feature of the moving shape:
    /// 0 = edge 1 (left/top), 1 = center, 2 = edge 2 (right/bottom), 3 = size
    sal_uInt8 nFeature { 0 };
    /// index of the winning candidate rectangle in rCandidates
    /// (-1 when not valid); identifies the reference shape for
    /// validation and redrawing the guide after final constraints
    sal_Int32 nCandidate { -1 };
    /// line segments to display; one for edge/center matches (spanning the
    /// compared shapes), two for size matches (one dimension mark on the
    /// moving shape, one on the reference shape)
    std::vector<SmartGuideSegment> aSegments;
};

/**
 * Pure, deterministic geometry for smart guides (Visio style dynamic
 * alignment and equal size guides). All values are in model units. No
 * model or view access: candidate rectangles are collected by the caller.
 *
 * Candidate selection is by displacement, not by stacking order: all
 * candidates and all of their features are evaluated, the match with the
 * smallest |delta| wins. Ties are broken by kind rank (Edge < Size <
 * Center, the SmartGuideKind enumeration order), then by perpendicular
 * distance of the two shapes' feature spans, then by stable geometric
 * coordinates (matched reference position, then the candidate rectangle
 * extents), which keeps the result independent of the candidate order.
 *
 * A match is only reported when the result is exact in integer model
 * units: a displacement that would place a feature on a half-integer
 * position (e.g. an odd-width center against an integer edge) is not
 * representable after rounding and is rejected.
 */
class SVXCORE_DLLPUBLIC SdrSmartGuide
{
public:
    /**
     * The three positions (edge 1, center, edge 2) of a rectangle on the
     * given axis.
     */
    static void getAxisPositions(
        const tools::Rectangle& rRect,
        bool bXAxis,
        double& rEdge1,
        double& rCenter,
        double& rEdge2);

    /**
     * Best alignment match on one axis, moving rMoving against
     * rCandidates. A match is valid when the required displacement
     * |nDelta| is at most fTolerance and the moved position is exact in
     * integer model units. The returned nDelta is the movement of the
     * whole shape; the guide line position equals nRefPos after the move.
     */
    static SmartGuideMatch findMoveAxisMatch(
        const tools::Rectangle& rMoving,
        const std::vector<tools::Rectangle>& rCandidates,
        bool bXAxis,
        double fTolerance);

    /**
     * Best alignment or equal-size match on one axis for a resize.
     * rOrig is the selection rectangle before the resize, aStart the
     * original handle position, aRef the resize reference point
     * (opposite corner, opposite side or the center for center-based
     * resize), aNow the current (not yet applied) handle position,
     * fCrossFact the factor of the other axis (1.0 when it is fixed)
     * used for the guide geometry. bAxisFixed disables the search (the
     * axis dimension is locked by a side handle).
     * Moving features are compared against all candidate features
     * (cross-feature pairs, e.g. dragging a right edge to a candidate's
     * left edge); equal-size snapping keeps the side the handle is on,
     * including reflected drags (sign of nMul / nDiv).
     * A valid nDelta is applied to aNow; the resulting dimension equals
     * the reference dimension for Size matches and the final handle
     * position is exact in integer model units.
     */
    static SmartGuideMatch findResizeAxisMatch(
        const tools::Rectangle& rOrig,
        const Point& aStart,
        const Point& aRef,
        const Point& aNow,
        const std::vector<tools::Rectangle>& rCandidates,
        bool bXAxis,
        bool bAxisFixed,
        double fCrossFact,
        double fTolerance);
};
