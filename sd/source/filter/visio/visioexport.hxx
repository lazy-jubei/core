/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <oox/core/xmlfilterbase.hxx>
#include <rtl/ustrbuf.hxx>

#include <com/sun/star/awt/Point.hpp>

#include <utility>
#include <vector>

namespace oox::core {

class VisioExport final : public XmlFilterBase
{
public:
    VisioExport(const css::uno::Reference<css::uno::XComponentContext>& rContext,
                const css::uno::Sequence<css::uno::Any>& rArguments);
    virtual ~VisioExport() override;

    // From FilterBase
    virtual bool importDocument() noexcept override;
    virtual bool exportDocument() override;

    // Only needed for import, leave empty
    virtual oox::vml::Drawing* getVmlDrawing() override { return nullptr; }
    virtual const oox::drawingml::Theme* getCurrentTheme() const override { return nullptr; }
    virtual oox::drawingml::table::TableStyleListPtr getTableStyles() override { return oox::drawingml::table::TableStyleListPtr(); }
    virtual oox::drawingml::chart::ChartConverter* getChartConverter() override { return nullptr; }

private:
    struct TextStyle
    {
        double mfFontSizeInches;
        double mfLeftMarginInches;
        double mfRightMarginInches;
        double mfTopMarginInches;
        double mfBottomMarginInches;
        sal_Int32 mnHorizontalAlign;
        sal_Int32 mnVerticalAlign;
    };

    // One VSDX Geometry section row, e.g. a RelMoveTo or RelLineTo. Cell
    // values are preformatted strings.
    struct GeometryCell
    {
        OUString aName;
        OUString aValue;
    };
    struct GeometryRow
    {
        OUString aRowType;
        std::vector<GeometryCell> aCells;
    };
    using Geometry = std::vector<GeometryRow>;

    virtual OUString SAL_CALL getImplementationName() override;
    virtual oox::ole::VbaProject* implCreateVbaProject() const override
    {
        return nullptr;
    }

    // Document structure
    void WriteDocumentXml();
    void WritePagesXml();
    void WritePageXml(sal_uInt32 nPageNum);

    // Shape tree
    void WriteShapeToBuilder(OUStringBuffer& rBuilder,
                             const css::uno::Reference<css::drawing::XShape>& xShape,
                             double fPageHeight);
    bool WriteTableToBuilder(OUStringBuffer& rBuilder,
                             const css::uno::Reference<css::drawing::XShape>& xShape,
                             double fPageHeight);
    void WriteRectangleToBuilder(OUStringBuffer& rBuilder, const OUString& rType,
                                 double fPinX, double fPinY, double fWidth,
                                 double fHeight, double fAngleRad,
                                 const OUString& rFillColor, bool bNoFill,
                                 const OUString& rLineColor, double fLineWidthInches,
                                 bool bLineVisible, const OUString& rText,
                                 const TextStyle& rTextStyle,
                                 const Geometry& rGeometry);

    // Geometry builders
    Geometry MakeRectangleGeometry() const;
    Geometry MakeEllipseGeometry(double fWidthInches, double fHeightInches) const;
    Geometry MakePointsGeometry(
        const css::uno::Sequence<css::uno::Sequence<css::awt::Point>>& rPointSequences,
        double fWidthInches, double fHeightInches, bool bCloseSubpaths) const;

    sal_uInt32 mnNextShapeId;
};

} // namespace oox::core

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
