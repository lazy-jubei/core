/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "visioexport.hxx"

#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/awt/FontUnderline.hpp>
#include <com/sun/star/awt/FontWeight.hpp>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XDrawPages.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/drawing/LineDash.hpp>
#include <com/sun/star/drawing/PolygonFlags.hpp>
#include <com/sun/star/drawing/PolyPolygonBezierCoords.hpp>
#include <com/sun/star/drawing/TextVerticalAdjust.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/document/XFilter.hpp>
#include <com/sun/star/document/XImporter.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/io/XSeekable.hpp>
#include <com/sun/star/lang/XMultiComponentFactory.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/container/XEnumeration.hpp>
#include <com/sun/star/container/XEnumerationAccess.hpp>
#include <com/sun/star/table/XTable.hpp>
#include <com/sun/star/style/LineSpacing.hpp>
#include <com/sun/star/style/LineSpacingMode.hpp>
#include <com/sun/star/style/ParagraphAdjust.hpp>
#include <com/sun/star/text/XTextRange.hpp>

#include <unotools/mediadescriptor.hxx>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <string_view>
#include <vector>

namespace {

constexpr double LO_TO_INCHES = 1.0 / 2540.0;
constexpr double DEFAULT_LINE_WIDTH_INCHES = 0.75 / 72.0;
constexpr double DEFAULT_FONT_SIZE_INCHES = 12.0 / 72.0;

void writeUtf8(const css::uno::Reference<css::io::XOutputStream>& xStream,
               const OUString& rText)
{
    if (!xStream.is())
        return;

    const OString aUtf8 = OUStringToOString(rText, RTL_TEXTENCODING_UTF8);
    xStream->writeBytes(css::uno::Sequence<sal_Int8>(
        reinterpret_cast<const sal_Int8*>(aUtf8.getStr()), aUtf8.getLength()));
    xStream->closeOutput();
}

css::awt::Size getPageSize(
    const css::uno::Reference<css::drawing::XDrawPage>& xPage)
{
    css::awt::Size aSize;
    css::uno::Reference<css::beans::XPropertySet> xPageProperties(
        xPage, css::uno::UNO_QUERY_THROW);
    xPageProperties->getPropertyValue(u"Width"_ustr) >>= aSize.Width;
    xPageProperties->getPropertyValue(u"Height"_ustr) >>= aSize.Height;
    return aSize;
}

OUString colorToHexSimple(sal_uInt32 nColor)
{
    // Use printf-style formatting for portability
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
                  static_cast<int>((nColor >> 16) & 0xff),
                  static_cast<int>((nColor >> 8) & 0xff),
                  static_cast<int>(nColor & 0xff));
    return OUString::createFromAscii(buf);
}

OUString fmtDouble(double fVal)
{
    if (fVal == std::floor(fVal))
        return OUString::number(static_cast<sal_Int32>(fVal));
    // Full precision like the reference VSDX files
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.16g", fVal);
    return OUString::createFromAscii(buf);
}

// The UNO FillTransparence property is a percent (0 = opaque, 100 = fully
// transparent), while the VSDX FillForegndTrans/FillBkgndTrans cells store
// a 0..1 fraction. Shapes without the property stay opaque.
double fillTransparencyToFraction(
    const css::uno::Reference<css::beans::XPropertySet>& xProperties)
{
    if (!xProperties.is())
        return 0.0;
    sal_Int16 nTransparence = 0;
    try
    {
        xProperties->getPropertyValue(u"FillTransparence"_ustr) >>= nTransparence;
    }
    catch (const css::uno::Exception&)
    {
        return 0.0;
    }
    if (nTransparence <= 0)
        return 0.0;
    if (nTransparence >= 100)
        return 1.0;
    return nTransparence / 100.0;
}

OUString escapeXml(const OUString& rText)
{
    OUString sEscaped = rText;
    sEscaped = sEscaped.replaceAll(u"&"_ustr, u"&amp;"_ustr);
    sEscaped = sEscaped.replaceAll(u"<"_ustr, u"&lt;"_ustr);
    sEscaped = sEscaped.replaceAll(u">"_ustr, u"&gt;"_ustr);
    sEscaped = sEscaped.replaceAll(u"\""_ustr, u"&quot;"_ustr);
    sEscaped = sEscaped.replaceAll(u"'"_ustr, u"&apos;"_ustr);
    return sEscaped;
}

// LineShape, PolyLineShape and PolyPolygonShape expose the PointSequenceSequence
// property "Geometry" - explicitly the *untransformed* point set
// (offapi/com/sun/star/drawing/PolyPolygonDescriptor.idl, mapped in
// svx/source/unodraw/unoprov.cxx). Points are in 1/100 mm, local to the shape.
css::uno::Sequence<css::uno::Sequence<css::awt::Point>>
getUntransformedPoints(const css::uno::Reference<css::beans::XPropertySet>& xProperties)
{
    css::uno::Sequence<css::uno::Sequence<css::awt::Point>> aPoints;
    if (!xProperties.is())
        return aPoints;
    try
    {
        xProperties->getPropertyValue(u"Geometry"_ustr) >>= aPoints;
    }
    catch (const css::uno::Exception&)
    {
        // Fall back to the reference points if the shape does not expose Geometry.
        try
        {
            xProperties->getPropertyValue(u"PolyPolygon"_ustr) >>= aPoints;
        }
        catch (const css::uno::Exception&)
        {
        }
    }
    return aPoints;
}

css::drawing::PolyPolygonBezierCoords getBezierPoints(
    const css::uno::Reference<css::beans::XPropertySet>& xProperties,
    const OUString& rPropertyName)
{
    css::drawing::PolyPolygonBezierCoords aBezier;
    if (!xProperties.is())
        return aBezier;
    try
    {
        xProperties->getPropertyValue(rPropertyName) >>= aBezier;
    }
    catch (const css::uno::Exception&)
    {
    }
    return aBezier;
}

bool hasNonemptyPolygon(const css::drawing::PolyPolygonBezierCoords& rBezier)
{
    for (sal_Int32 i = 0; i < rBezier.Coordinates.getLength(); ++i)
        if (rBezier.Coordinates[i].getLength() > 0)
            return true;
    return false;
}

// Map the imported controller marker names to the VSDX
// BeginArrow/EndArrow values. The polygon based recognition is
// authoritative, so this is only consulted for markers whose polygon is
// not recognized, and unknown names return 0.
sal_Int32 arrowTypeFromName(const OUString& rName)
{
    if (rName == u"Marker_0"_ustr)
        return 1; // Arrow
    if (rName == u"Marker_1"_ustr)
        return 13; // Long filled triangle
    if (rName == u"Marker_2"_ustr)
        return 2; // Triangle
    if (rName == u"Marker_3"_ustr)
        return 5; // Concave curved marker
    return 0;
}

// The first coordinate sequence of a marker polygon identifies the
// silhouette. A repeated closing point is not an extra vertex, and Bezier
// control points are read from the matching flag sequence.
sal_Int32 arrowTypeFromFirstPolygon(
    const css::drawing::PolyPolygonBezierCoords& rBezier)
{
    if (rBezier.Coordinates.getLength() == 0)
        return 0;
    const auto& rPoints = rBezier.Coordinates[0];
    sal_Int32 nCount = rPoints.getLength();
    if (nCount >= 2 && rPoints[nCount - 1].X == rPoints[0].X
        && rPoints[nCount - 1].Y == rPoints[0].Y)
        --nCount;
    if (nCount < 3)
        return 0;

    bool bHasControl = false;
    if (rBezier.Flags.getLength() > 0)
    {
        const auto& rFlags = rBezier.Flags[0];
        for (sal_Int32 i = 0; i < rFlags.getLength() && i < nCount; ++i)
            if (rFlags[i] == css::drawing::PolygonFlags_CONTROL)
                bHasControl = true;
    }

    // Count the unique vertices; repeats cannot identify a silhouette.
    sal_Int32 nUnique = 0;
    for (sal_Int32 i = 0; i < nCount; ++i)
    {
        bool bKnown = false;
        for (sal_Int32 j = 0; j < i; ++j)
            if (rPoints[i].X == rPoints[j].X && rPoints[i].Y == rPoints[j].Y)
            {
                bKnown = true;
                break;
            }
        if (!bKnown)
            ++nUnique;
    }

    // A curved 5-ish silhouette with control points is the concave curved
    // marker.
    if (bHasControl && nUnique >= 4 && nUnique <= 6)
        return 5; // Concave curved marker
    if (nUnique >= 10)
        return 1; // Arrow
    if (nUnique == 3)
    {
        double fMinX = rPoints[0].X;
        double fMaxX = rPoints[0].X;
        double fMinY = rPoints[0].Y;
        double fMaxY = rPoints[0].Y;
        for (sal_Int32 i = 1; i < nCount; ++i)
        {
            const double x = rPoints[i].X;
            const double y = rPoints[i].Y;
            if (x < fMinX)
                fMinX = x;
            if (x > fMaxX)
                fMaxX = x;
            if (y < fMinY)
                fMinY = y;
            if (y > fMaxY)
                fMaxY = y;
        }
        const double fW = fMaxX - fMinX;
        const double fH = fMaxY - fMinY;
        if (fW <= 0.0 || fH <= 0.0)
            return 0;
        // A triangle as tall as it is wide is a long filled triangle.
        return fH >= fW ? 13 : 2;
    }
    return 0;
}

// The marker polygon is authoritative: a recognizable silhouette wins even
// when the marker name is only a generic label (as UNO supplies for
// user-assigned markers). The imported controller names act as a fallback
// for unrecognized polygons, and any other active marker exports as a plain
// triangle.
sal_Int32 getArrowType(const OUString& rName,
                       const css::drawing::PolyPolygonBezierCoords& rPolygon)
{
    const sal_Int32 nPolygonType = arrowTypeFromFirstPolygon(rPolygon);
    if (nPolygonType != 0)
        return nPolygonType;
    const sal_Int32 nNameType = arrowTypeFromName(rName);
    if (nNameType != 0)
        return nNameType;
    // Any other active marker exports as a plain triangle.
    return 2;
}

// Map an arrow width (1/100 mm; negative values are a percentage of the line
// width) to the VSDX BeginArrowSize/EndArrowSize index by comparing it with
// the "medium" width for the current line weight.
sal_Int32 getArrowSizeBucket(sal_Int32 nWidthHundredthsMm, double fLineWidthInches)
{
    double fWidthMm = 0.0;
    if (nWidthHundredthsMm > 0)
        fWidthMm = nWidthHundredthsMm / 100.0;
    else
        fWidthMm = std::abs(static_cast<double>(nWidthHundredthsMm))
                   * fLineWidthInches * 25.4 / 100.0;
    const double fMediumMm
        = 25.4 * (0.1 / (fLineWidthInches * fLineWidthInches + 1.0)
                  + 2.54 * fLineWidthInches);
    static constexpr double FACTORS[] = { 0.5, 0.75, 1.0, 1.5, 2.0 };
    const double fRatio = fWidthMm / fMediumMm;
    sal_Int32 nBest = 2;
    double fBestDistance = 1e300;
    for (sal_Int32 i = 0; i < 5; ++i)
    {
        const double fDistance = std::abs(fRatio - FACTORS[i]);
        if (fDistance < fBestDistance)
        {
            fBestDistance = fDistance;
            nBest = i;
        }
    }
    return nBest;
}

} // anonymous namespace

namespace oox::core {

VisioExport::VisioExport(const css::uno::Reference<css::uno::XComponentContext>& rContext,
                         const css::uno::Sequence<css::uno::Any>& rArguments)
    : XmlFilterBase(rContext)
    , mnNextShapeId(100)
{
    (void)rArguments;
}

VisioExport::~VisioExport() = default;

OUString VisioExport::getImplementationName()
{
    return u"com.sun.star.comp.Draw.VisioExportFilter"_ustr;
}

bool VisioExport::importDocument() noexcept
{
    // Save As/Save a Copy only offers filters which support both import and
    // export. Delegate the import side to LibreOffice's existing Visio filter
    // so this service can safely be advertised in those dialogs as well.
    try
    {
        const auto& xContext = getComponentContext();
        css::uno::Reference<css::uno::XInterface> xImportService(
            xContext->getServiceManager()->createInstanceWithContext(
                u"com.sun.star.comp.Draw.VisioImportFilter"_ustr, xContext));
        css::uno::Reference<css::document::XImporter> xImporter(
            xImportService, css::uno::UNO_QUERY_THROW);
        css::uno::Reference<css::document::XFilter> xFilter(
            xImportService, css::uno::UNO_QUERY_THROW);

        // XmlFilterBase has already inspected the package through this stream.
        // Rewind it before handing it to the writerperfect Visio importer.
        css::uno::Reference<css::io::XInputStream> xInputStream
            = getMediaDescriptor().getUnpackedValueOrDefault(
                utl::MediaDescriptor::PROP_INPUTSTREAM,
                css::uno::Reference<css::io::XInputStream>());
        css::uno::Reference<css::io::XSeekable> xSeekable(xInputStream, css::uno::UNO_QUERY);
        if (xSeekable.is())
            xSeekable->seek(0);

        xImporter->setTargetDocument(getModel());
        return xFilter->filter(getMediaDescriptor().getAsConstPropertyValueList());
    }
    catch (...)
    {
        return false;
    }
}

bool VisioExport::exportDocument()
{
    // Get the SdXImpressDocument from the model
    auto xModel = getModel();
    if (!xModel.is())
        return false;

    // Get the SdModel (internal document model)
    css::uno::Reference<css::drawing::XDrawPagesSupplier> xDrawPagesSupplier(
        xModel, css::uno::UNO_QUERY);
    if (!xDrawPagesSupplier.is())
        return false;

    auto xDrawPages = xDrawPagesSupplier->getDrawPages();
    if (!xDrawPages.is())
        return false;

    const sal_Int32 nPageCount = xDrawPages->getCount();
    if (nPageCount == 0)
        return false;

    mnNextShapeId = 100;

    // Let LibreOffice's package storage generate [Content_Types].xml and
    // relationship parts. Writing those streams manually bypasses the package
    // metadata maintained by XmlFilterBase.
    addRelation(u"http://schemas.microsoft.com/visio/2010/relationships/document"_ustr,
                u"visio/document.xml");
    addRelation(
        u"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"_ustr,
        u"docProps/core.xml");
    addRelation(
        u"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties"_ustr,
        u"docProps/app.xml");

    // Write visio/document.xml
    WriteDocumentXml();

    // Write visio/windows.xml
    {
        const OUString sWindows(std::u16string_view(
            u"<?xml version='1.0' encoding='utf-8' ?>\n"
            u"<Windows xmlns='http://schemas.microsoft.com/office/visio/2012/main'>"
            u"<Window ID='0' WindowType='Drawing' WindowState='1073741824' "
            u"WindowLeft='-1' WindowTop='-1' WindowRight='100' WindowBottom='100'>"
            u"<ViewScale>1</ViewScale><ViewLeft>0</ViewLeft><ViewTop>0</ViewTop>"
            u"</Window></Windows>"));
        writeUtf8(openFragmentStream(
                      u"visio/windows.xml"_ustr,
                      u"application/vnd.ms-visio.windows+xml"_ustr),
                  sWindows);
    }

    // Write docProps/core.xml
    {
        const OUString sCore(std::u16string_view(
            u"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            u"<cp:coreProperties "
            u"xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
            u"xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
            u"xmlns:dcterms=\"http://purl.org/dc/terms/\" "
            u"xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">"
            u"<dc:creator>LibreOffice</dc:creator>"
            u"<cp:lastModifiedBy>LibreOffice</cp:lastModifiedBy>"
            u"</cp:coreProperties>"));
        writeUtf8(openFragmentStream(
                      u"docProps/core.xml"_ustr,
                      u"application/vnd.openxmlformats-package.core-properties+xml"_ustr),
                  sCore);
    }

    // Write docProps/app.xml
    {
        const OUString sApp(std::u16string_view(
            u"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            u"<Properties "
            u"xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
            u"<Application>LibreOffice Draw</Application>"
            u"</Properties>"));
        writeUtf8(openFragmentStream(
                      u"docProps/app.xml"_ustr,
                      u"application/vnd.openxmlformats-officedocument.extended-properties+xml"_ustr),
                  sApp);
    }

    // Write visio/pages/pages.xml
    WritePagesXml();

    // Write each page
    for (sal_Int32 i = 0; i < nPageCount; i++)
    {
        WritePageXml(i);
    }

    // Commit the storage
    commitStorage();
    return true;
}

void VisioExport::WriteDocumentXml()
{
    // Build the document.xml with DocumentSettings, Colors, FaceNames, StyleSheets, DocumentSheet
    const OUString sDoc(std::u16string_view(
        u"<?xml version='1.0' encoding='utf-8' ?>\n"
        u"<VisioDocument xmlns='http://schemas.microsoft.com/office/visio/2012/main' "
        u"xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships' "
        u"xml:space='preserve'>"
        u"<DocumentSettings TopPage='0' DefaultTextStyle='0' DefaultLineStyle='0' "
        u"DefaultFillStyle='0' DefaultGuideStyle='0'>"
        u"<GlueSettings>9</GlueSettings>"
        u"<SnapSettings>295</SnapSettings>"
        u"<SnapExtensions>34</SnapExtensions>"
        u"<SnapAngles/>"
        u"<DynamicGridEnabled>1</DynamicGridEnabled>"
        u"<ProtectStyles>0</ProtectStyles>"
        u"<ProtectShapes>0</ProtectShapes>"
        u"<ProtectMasters>0</ProtectMasters>"
        u"<ProtectBkgnds>0</ProtectBkgnds>"
        u"</DocumentSettings>"
        u"<Colors>"
        u"<ColorEntry IX='0' RGB='#000000'/>"
        u"<ColorEntry IX='1' RGB='#ffffff'/>"
        u"</Colors>"
        u"<FaceNames>"
        u"<FaceName NameU='Calibri' "
        u"UnicodeRanges='-469750017 -1040178053 9 0' "
        u"CharSets='536871423 0' "
        u"Panose='2 15 5 2 2 2 4 3 2 4' "
        u"Flags='357'/>"
        u"</FaceNames>"
        u"<StyleSheets>"
        u"<StyleSheet ID='0' NameU='No Style' IsCustomNameU='1' Name='No Style' IsCustomName='1'>"
        u"<Cell N='EnableLineProps' V='1'/>"
        u"<Cell N='EnableFillProps' V='1'/>"
        u"<Cell N='EnableTextProps' V='1'/>"
        u"<Cell N='HideForApply' V='0'/>"
        u"<Cell N='LineWeight' V='0.01041666666666667'/>"
        u"<Cell N='LineColor' V='0'/>"
        u"<Cell N='LinePattern' V='1'/>"
        u"<Cell N='Rounding' V='0'/>"
        u"<Cell N='EndArrowSize' V='2'/>"
        u"<Cell N='BeginArrow' V='0'/>"
        u"<Cell N='EndArrow' V='0'/>"
        u"<Cell N='LineCap' V='0'/>"
        u"<Cell N='BeginArrowSize' V='2'/>"
        u"<Cell N='LineColorTrans' V='0'/>"
        u"<Cell N='CompoundType' V='0'/>"
        u"<Cell N='FillForegnd' V='1'/>"
        u"<Cell N='FillBkgnd' V='0'/>"
        u"<Cell N='FillPattern' V='1'/>"
        u"<Cell N='ShdwForegnd' V='0'/>"
        u"<Cell N='ShdwPattern' V='0'/>"
        u"<Cell N='FillForegndTrans' V='0'/>"
        u"<Cell N='FillBkgndTrans' V='0'/>"
        u"<Cell N='ShdwForegndTrans' V='0'/>"
        u"<Cell N='ShapeShdwType' V='0'/>"
        u"<Cell N='ShapeShdwOffsetX' V='0'/>"
        u"<Cell N='ShapeShdwOffsetY' V='0'/>"
        u"<Cell N='ShapeShdwObliqueAngle' V='0'/>"
        u"<Cell N='ShapeShdwScaleFactor' V='1'/>"
        u"<Cell N='ShapeShdwBlur' V='0'/>"
        u"<Cell N='ShapeShdwShow' V='0'/>"
        u"<Cell N='LeftMargin' V='0'/>"
        u"<Cell N='RightMargin' V='0'/>"
        u"<Cell N='TopMargin' V='0'/>"
        u"<Cell N='BottomMargin' V='0'/>"
        u"<Cell N='VerticalAlign' V='1'/>"
        u"<Cell N='TextBkgnd' V='0'/>"
        u"<Cell N='DefaultTabStop' V='0.5905511811023622'/>"
        u"<Cell N='TextDirection' V='0'/>"
        u"<Cell N='TextBkgndTrans' V='0'/>"
        u"<Section N='Character'><Row IX='0'>"
        u"<Cell N='Font' V='Calibri'/>"
        u"<Cell N='Color' V='0'/>"
        u"<Cell N='Style' V='0'/>"
        u"<Cell N='Case' V='0'/>"
        u"<Cell N='Pos' V='0'/>"
        u"<Cell N='FontScale' V='1'/>"
        u"<Cell N='Size' V='0.1666666666666667'/>"
        u"</Row></Section>"
        u"<Section N='Paragraph'><Row IX='0'>"
        u"<Cell N='IndFirst' V='0'/>"
        u"<Cell N='IndLeft' V='0'/>"
        u"<Cell N='IndRight' V='0'/>"
        u"<Cell N='SpLine' V='-1.2'/>"
        u"<Cell N='SpBefore' V='0'/>"
        u"<Cell N='SpAfter' V='0'/>"
        u"<Cell N='HorzAlign' V='1'/>"
        u"<Cell N='Bullet' V='0'/>"
        u"<Cell N='BulletStr' V=''/>"
        u"<Cell N='BulletFont' V='0'/>"
        u"<Cell N='BulletFontSize' V='-1'/>"
        u"<Cell N='TextPosAfterBullet' V='0'/>"
        u"<Cell N='Flags' V='0'/>"
        u"</Row></Section>"
        u"</StyleSheet>"
        u"</StyleSheets>"
        u"<DocumentSheet NameU='TheDoc' IsCustomNameU='1' Name='TheDoc' IsCustomName='1' "
        u"LineStyle='0' FillStyle='0' TextStyle='0'>"
        u"<Cell N='OutputFormat' V='0'/>"
        u"<Cell N='LockPreview' V='0'/>"
        u"<Cell N='AddMarkup' V='0'/>"
        u"<Cell N='ViewMarkup' V='0'/>"
        u"<Cell N='DocLockReplace' V='0' U='BOOL'/>"
        u"<Cell N='NoCoauth' V='0' U='BOOL'/>"
        u"<Cell N='DocLockDuplicatePage' V='0' U='BOOL'/>"
        u"<Cell N='PreviewQuality' V='0'/>"
        u"<Cell N='PreviewScope' V='0'/>"
        u"<Cell N='DocLangID' V='en-US'/>"
        u"</DocumentSheet>"
        u"</VisioDocument>"));

    auto xStream = openFragmentStream(
        u"visio/document.xml"_ustr,
        u"application/vnd.ms-visio.drawing.main+xml"_ustr);
    addRelation(xStream,
                u"http://schemas.microsoft.com/visio/2010/relationships/pages"_ustr,
                u"pages/pages.xml");
    addRelation(xStream,
                u"http://schemas.microsoft.com/visio/2010/relationships/windows"_ustr,
                u"windows.xml");
    writeUtf8(xStream, sDoc);
}

void VisioExport::WritePagesXml()
{
    auto xModel = getModel();
    css::uno::Reference<css::drawing::XDrawPagesSupplier> xDrawPagesSupplier(
        xModel, css::uno::UNO_QUERY_THROW);
    auto xDrawPages = xDrawPagesSupplier->getDrawPages();
    const sal_Int32 nCount = xDrawPages->getCount();

    OUStringBuffer aBuilder;
    aBuilder.append(u"<?xml version='1.0' encoding='utf-8' ?>\n");
    aBuilder.append(u"<Pages xmlns='http://schemas.microsoft.com/office/visio/2012/main' "
                    u"xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships' "
                    u"xml:space='preserve'>");

    for (sal_Int32 i = 0; i < nCount; i++)
    {
        css::uno::Reference<css::drawing::XDrawPage> xPage(
            xDrawPages->getByIndex(i), css::uno::UNO_QUERY_THROW);
        const css::awt::Size aPageSize = getPageSize(xPage);

        // Convert from 1/100 mm to inches
        double fWidth = aPageSize.Width * LO_TO_INCHES;
        double fHeight = aPageSize.Height * LO_TO_INCHES;

        // Page IDs are 0-based in VSDX
        aBuilder.append(u"<Page ID='");
        aBuilder.append(OUString::number(i));
        aBuilder.append(u"' NameU='Page-");
        aBuilder.append(OUString::number(i + 1));
        aBuilder.append(u"' Name='Page-");
        aBuilder.append(OUString::number(i + 1));
        aBuilder.append(u"' ViewScale='1' "
                        u"ViewCenterX='");
        aBuilder.append(fmtDouble(fWidth / 2.0));
        aBuilder.append(u"' ViewCenterY='");
        aBuilder.append(fmtDouble(fHeight / 2.0));
        aBuilder.append(u"'>");
        aBuilder.append(u"<PageSheet LineStyle='0' FillStyle='0' TextStyle='0'>");
        aBuilder.append(u"<Cell N='PageWidth' V='");
        aBuilder.append(fmtDouble(fWidth));
        aBuilder.append(u"'/>");
        aBuilder.append(u"<Cell N='PageHeight' V='");
        aBuilder.append(fmtDouble(fHeight));
        aBuilder.append(u"'/>");
        aBuilder.append(u"<Cell N='PageScale' V='0.03937007874015748' U='MM'/>");
        aBuilder.append(u"<Cell N='DrawingScale' V='0.03937007874015748' U='MM'/>");
        aBuilder.append(u"<Cell N='DrawingSizeType' V='0'/>");
        aBuilder.append(u"<Cell N='DrawingScaleType' V='0'/>");
        aBuilder.append(u"<Cell N='InhibitSnap' V='0'/>");
        aBuilder.append(u"<Cell N='PageLockReplace' V='0' U='BOOL'/>");
        aBuilder.append(u"<Cell N='PageLockDuplicate' V='0' U='BOOL'/>");
        aBuilder.append(u"<Cell N='UIVisibility' V='0'/>");
        aBuilder.append(u"<Cell N='ShdwType' V='0'/>");
        aBuilder.append(u"<Cell N='DrawingResizeType' V='1'/>");
        aBuilder.append(u"<Cell N='RouteStyle' V='6'/>");
        aBuilder.append(u"<Cell N='PageShapeSplit' V='1'/>");
        aBuilder.append(u"<Cell N='PrintPageOrientation' V='2'/>");
        aBuilder.append(u"</PageSheet>");
        // CRITICAL: Rel element links page to its content file
        aBuilder.append(u"<Rel r:id='rId");
        aBuilder.append(OUString::number(i + 1));
        aBuilder.append(u"'/>");
        aBuilder.append(u"</Page>");
    }

    aBuilder.append(u"</Pages>");

    auto xStream = openFragmentStream(
        u"visio/pages/pages.xml"_ustr,
        u"application/vnd.ms-visio.pages+xml"_ustr);
    for (sal_Int32 i = 1; i <= nCount; ++i)
    {
        const OUString sTarget = u"page"_ustr + OUString::number(i) + u".xml";
        addRelation(xStream,
                    u"http://schemas.microsoft.com/visio/2010/relationships/page"_ustr,
                    sTarget);
    }
    writeUtf8(xStream, aBuilder.makeStringAndClear());
}

void VisioExport::WritePageXml(sal_uInt32 nPageNum)
{
    auto xModel = getModel();
    css::uno::Reference<css::drawing::XDrawPagesSupplier> xDrawPagesSupplier(
        xModel, css::uno::UNO_QUERY_THROW);
    auto xDrawPages = xDrawPagesSupplier->getDrawPages();
    css::uno::Reference<css::drawing::XDrawPage> xPage(
        xDrawPages->getByIndex(nPageNum), css::uno::UNO_QUERY_THROW);
    const double fPageHeight = getPageSize(xPage).Height * LO_TO_INCHES;

    OUStringBuffer aBuilder;
    aBuilder.append(u"<?xml version='1.0' encoding='utf-8' ?>\n");
    aBuilder.append(u"<PageContents xmlns='http://schemas.microsoft.com/office/visio/2012/main' "
                    u"xmlns:r='http://schemas.openxmlformats.org/officeDocument/2006/relationships' "
                    u"xml:space='preserve'>");
    aBuilder.append(u"<Shapes>");

    // Get shapes on this page
    css::uno::Reference<css::drawing::XShapes> xShapes(
        xPage, css::uno::UNO_QUERY_THROW);
    const sal_Int32 nShapeCount = xShapes->getCount();

    for (sal_Int32 j = 0; j < nShapeCount; j++)
    {
        css::uno::Reference<css::drawing::XShape> xShape(
            xShapes->getByIndex(j), css::uno::UNO_QUERY_THROW);
        WriteShapeToBuilder(aBuilder, xShape, fPageHeight);
    }

    aBuilder.append(u"</Shapes>");
    aBuilder.append(u"</PageContents>");

    OUString sPageName = u"visio/pages/page" + OUString::number(nPageNum + 1) + u".xml";
    writeUtf8(openFragmentStream(
                  sPageName, u"application/vnd.ms-visio.page+xml"_ustr),
              aBuilder.makeStringAndClear());
}

void VisioExport::CollectTextRuns(
    const css::uno::Reference<css::text::XText>& xText,
    TextStyle& rTextStyle) const
{
    css::uno::Reference<css::container::XEnumerationAccess> xParagraphAccess(
        xText, css::uno::UNO_QUERY);
    if (!xParagraphAccess.is())
        return;

    try
    {
        css::uno::Reference<css::container::XEnumeration> xParagraphs(
            xParagraphAccess->createEnumeration(), css::uno::UNO_SET_THROW);
        while (xParagraphs->hasMoreElements())
        {
            css::uno::Any aParagraph = xParagraphs->nextElement();
            css::uno::Reference<css::container::XEnumerationAccess> xPortionAccess(
                aParagraph, css::uno::UNO_QUERY);
            if (!xPortionAccess.is())
                continue;

            ParagraphStyle aParagraphStyle;
            aParagraphStyle.mnHorizontalAlign = rTextStyle.mnHorizontalAlign;
            css::uno::Reference<css::beans::XPropertySet> xParagraphProperties(
                aParagraph, css::uno::UNO_QUERY);
            if (xParagraphProperties.is())
            {
                sal_Int16 nParagraphAdjust
                    = static_cast<sal_Int16>(css::style::ParagraphAdjust_LEFT);
                if (xParagraphProperties->getPropertyValue(u"ParaAdjust"_ustr)
                    >>= nParagraphAdjust)
                {
                    const auto eParagraphAdjust
                        = static_cast<css::style::ParagraphAdjust>(nParagraphAdjust);
                    if (eParagraphAdjust == css::style::ParagraphAdjust_CENTER)
                        aParagraphStyle.mnHorizontalAlign = 1;
                    else if (eParagraphAdjust == css::style::ParagraphAdjust_RIGHT)
                        aParagraphStyle.mnHorizontalAlign = 2;
                    else if (eParagraphAdjust == css::style::ParagraphAdjust_BLOCK)
                        aParagraphStyle.mnHorizontalAlign = 3;
                    else
                        aParagraphStyle.mnHorizontalAlign = 0;
                }
                css::style::LineSpacing aLineSpacing;
                if ((xParagraphProperties->getPropertyValue(u"ParaLineSpacing"_ustr)
                     >>= aLineSpacing)
                    && aLineSpacing.Mode == css::style::LineSpacingMode::PROP)
                    aParagraphStyle.mfLineSpacing
                        = -static_cast<double>(aLineSpacing.Height) / 100.0;
            }
            const sal_uInt32 nParagraphIndex = rTextStyle.maParagraphs.size();
            rTextStyle.maParagraphs.push_back(aParagraphStyle);

            css::uno::Reference<css::container::XEnumeration> xPortions(
                xPortionAccess->createEnumeration(), css::uno::UNO_SET_THROW);
            bool bFirstPortion = true;
            while (xPortions->hasMoreElements())
            {
                css::uno::Reference<css::text::XTextRange> xTextRange(
                    xPortions->nextElement(), css::uno::UNO_QUERY);
                if (!xTextRange.is())
                    continue;

                TextRun aRun;
                aRun.maText = xTextRange->getString();
                aRun.mfFontSizeInches = rTextStyle.mfFontSizeInches;
                aRun.maColor = rTextStyle.maColor;
                aRun.mbParagraphStart = bFirstPortion;
                aRun.mnParagraphIndex = nParagraphIndex;

                css::uno::Reference<css::beans::XPropertySet> xRunProperties(
                    xTextRange, css::uno::UNO_QUERY);
                if (xRunProperties.is())
                {
                    float fCharHeight = 0.0f;
                    if ((xRunProperties->getPropertyValue(u"CharHeight"_ustr)
                         >>= fCharHeight)
                        && fCharHeight > 0.0f)
                    {
                        aRun.mfFontSizeInches = fCharHeight / 72.0;
                    }

                    sal_Int32 nCharColor = -1;
                    if ((xRunProperties->getPropertyValue(u"CharColor"_ustr)
                         >>= nCharColor)
                        && nCharColor >= 0)
                    {
                        aRun.maColor = colorToHexSimple(
                            static_cast<sal_uInt32>(nCharColor));
                    }

                    OUString sFontName;
                    if ((xRunProperties->getPropertyValue(u"CharFontName"_ustr)
                         >>= sFontName)
                        && !sFontName.isEmpty())
                    {
                        aRun.maFontName = sFontName;
                    }

                    float fCharWeight = css::awt::FontWeight::NORMAL;
                    if ((xRunProperties->getPropertyValue(u"CharWeight"_ustr)
                         >>= fCharWeight)
                        && fCharWeight > css::awt::FontWeight::NORMAL)
                    {
                        aRun.mnStyle |= 1;
                    }

                    css::awt::FontSlant eCharPosture = css::awt::FontSlant_NONE;
                    if ((xRunProperties->getPropertyValue(u"CharPosture"_ustr)
                         >>= eCharPosture)
                        && eCharPosture != css::awt::FontSlant_NONE)
                    {
                        aRun.mnStyle |= 2;
                    }

                    sal_Int16 nCharUnderline = css::awt::FontUnderline::NONE;
                    if ((xRunProperties->getPropertyValue(u"CharUnderline"_ustr)
                         >>= nCharUnderline)
                        && nCharUnderline != css::awt::FontUnderline::NONE)
                    {
                        aRun.mnStyle |= 4;
                    }
                }

                rTextStyle.maRuns.push_back(std::move(aRun));
                bFirstPortion = false;
            }
            if (bFirstPortion)
            {
                TextRun aEmptyRun;
                aEmptyRun.mfFontSizeInches = rTextStyle.mfFontSizeInches;
                aEmptyRun.maColor = rTextStyle.maColor;
                aEmptyRun.mbParagraphStart = true;
                aEmptyRun.mnParagraphIndex = nParagraphIndex;
                rTextStyle.maRuns.push_back(std::move(aEmptyRun));
            }
        }
    }
    catch (const css::uno::Exception&)
    {
        // Fall back to the shape-level text properties.
        rTextStyle.maRuns.clear();
        rTextStyle.maParagraphs.clear();
    }
}

void VisioExport::WriteShapeToBuilder(OUStringBuffer& rBuilder,
                                       const css::uno::Reference<css::drawing::XShape>& xShape,
                                       double fPageHeight)
{
    // Visio imports commonly represent one logical master as a hierarchy of
    // Draw groups.  The visual content (including fills and text) lives on the
    // children, while the group itself is only a container.  Export the leaf
    // shapes in page coordinates instead of replacing every group with one
    // empty rectangular placeholder.
    const OUString sServiceName = xShape->getShapeType();
    if (sServiceName == u"com.sun.star.drawing.GroupShape"_ustr)
    {
        css::uno::Reference<css::drawing::XShapes> xGroupShapes(
            xShape, css::uno::UNO_QUERY);
        if (xGroupShapes.is())
        {
            const sal_Int32 nChildCount = xGroupShapes->getCount();
            for (sal_Int32 nChild = 0; nChild < nChildCount; ++nChild)
            {
                css::uno::Reference<css::drawing::XShape> xChild(
                    xGroupShapes->getByIndex(nChild), css::uno::UNO_QUERY_THROW);
                WriteShapeToBuilder(rBuilder, xChild, fPageHeight);
            }
            return;
        }
    }

    // Get basic properties
    const css::awt::Point aPos = xShape->getPosition();
    const css::awt::Size aSize = xShape->getSize();

    // Convert from 1/100 mm to inches
    double fWidth = aSize.Width * LO_TO_INCHES;
    double fHeight = aSize.Height * LO_TO_INCHES;
    // Pin is at center
    double fPinX = (aPos.X + aSize.Width / 2.0) * LO_TO_INCHES;
    double fPinY_raw = (aPos.Y + aSize.Height / 2.0) * LO_TO_INCHES;
    // Visio uses bottom-left origin (Y up); LO uses top-left (Y down)
    double fPinY = fPageHeight - fPinY_raw;

    css::uno::Reference<css::beans::XPropertySet> xProperties(
        xShape, css::uno::UNO_QUERY);

    // Get rotation
    sal_Int32 nRotationHundredths = 0;
    try
    {
        if (xProperties.is())
            xProperties->getPropertyValue(u"RotateAngle"_ustr) >>= nRotationHundredths;
    }
    catch (const css::uno::Exception&)
    {
    }
    double fAngleRad = nRotationHundredths * std::numbers::pi / 18000.0;
    // LineShape endpoint geometry already encodes direction; RotateAngle is
    // derived from the endpoints and redundant here.
    if (sServiceName == u"com.sun.star.drawing.LineShape"_ustr)
        fAngleRad = 0.0;

    // Match the dispatch used by LibreOffice's existing shape exporters.
    if (sServiceName == u"com.sun.star.drawing.TableShape"_ustr
        || sServiceName == u"com.sun.star.presentation.TableShape"_ustr)
    {
        if (WriteTableToBuilder(rBuilder, xShape, fPageHeight))
            return;
    }

    OUString sType = u"Shape"_ustr;
    if (sServiceName == u"com.sun.star.drawing.ConnectorShape"_ustr)
        sType = u"Connector"_ustr;

    // Get fill/line properties
    OUString sFillColor = u"#ffffff"_ustr;
    bool bNoFill = false;
    double fFillTransparency = 0.0;
    try
    {
        css::drawing::FillStyle eFillStyle = css::drawing::FillStyle_SOLID;
        if (xProperties.is())
            xProperties->getPropertyValue(u"FillStyle"_ustr) >>= eFillStyle;
        if (eFillStyle == css::drawing::FillStyle_NONE)
        {
            bNoFill = true;
        }
        else if (xProperties.is())
        {
            sal_Int32 nColor = 0xffffff;
            xProperties->getPropertyValue(u"FillColor"_ustr) >>= nColor;
            sFillColor = colorToHexSimple(static_cast<sal_uInt32>(nColor));
            fFillTransparency = fillTransparencyToFraction(xProperties);
        }
    }
    catch (const css::uno::Exception&)
    {
    }

    OUString sLineColor = u"#000000"_ustr;
    double fLineWidthInches = DEFAULT_LINE_WIDTH_INCHES;
    bool bLineVisible = true;
    try
    {
        css::drawing::LineStyle eLineStyle = css::drawing::LineStyle_SOLID;
        if (xProperties.is())
            xProperties->getPropertyValue(u"LineStyle"_ustr) >>= eLineStyle;
        if (eLineStyle == css::drawing::LineStyle_NONE)
        {
            bLineVisible = false;
        }
        else if (xProperties.is())
        {
            sal_Int32 nColor = 0;
            xProperties->getPropertyValue(u"LineColor"_ustr) >>= nColor;
            sLineColor = colorToHexSimple(static_cast<sal_uInt32>(nColor));
            sal_Int32 nLineWidth = 0;
            xProperties->getPropertyValue(u"LineWidth"_ustr) >>= nLineWidth;
            // LineWidth is in 1/100 mm; VSDX stores in inches (U='PT' is display unit)
            fLineWidthInches = nLineWidth * LO_TO_INCHES;
        }
    }
    catch (const css::uno::Exception&)
    {
    }

    // Arrow markers on the line ends. LineStart/LineEnd hold the marker
    // polygon, LineStartName/LineEndName the marker name that carries the
    // exact Visio marker across the import/export round trip, and
    // LineStartWidth/LineEndWidth the width in 1/100 mm (negative values are
    // a percentage of the line width).
    sal_Int32 nBeginType = 0;
    sal_Int32 nBeginSize = 2;
    sal_Int32 nEndType = 0;
    sal_Int32 nEndSize = 2;
    if (bLineVisible)
    {
        css::drawing::PolyPolygonBezierCoords aLineStart;
        css::drawing::PolyPolygonBezierCoords aLineEnd;
        OUString sLineStartName;
        OUString sLineEndName;
        sal_Int32 nLineStartWidth = 0;
        sal_Int32 nLineEndWidth = 0;
        try
        {
            if (xProperties.is())
            {
                xProperties->getPropertyValue(u"LineStart"_ustr) >>= aLineStart;
                xProperties->getPropertyValue(u"LineEnd"_ustr) >>= aLineEnd;
                xProperties->getPropertyValue(u"LineStartName"_ustr) >>= sLineStartName;
                xProperties->getPropertyValue(u"LineEndName"_ustr) >>= sLineEndName;
                xProperties->getPropertyValue(u"LineStartWidth"_ustr) >>= nLineStartWidth;
                xProperties->getPropertyValue(u"LineEndWidth"_ustr) >>= nLineEndWidth;
            }
        }
        catch (const css::uno::Exception&)
        {
        }

        // An end is active when the line is visible, the marker polygon is
        // nonempty and the marker width is nonzero.
        if (hasNonemptyPolygon(aLineStart) && nLineStartWidth != 0)
        {
            nBeginType = getArrowType(sLineStartName, aLineStart);
            nBeginSize = getArrowSizeBucket(nLineStartWidth, fLineWidthInches);
        }
        if (hasNonemptyPolygon(aLineEnd) && nLineEndWidth != 0)
        {
            nEndType = getArrowType(sLineEndName, aLineEnd);
            nEndSize = getArrowSizeBucket(nLineEndWidth, fLineWidthInches);
        }
    }

    // Get text
    OUString sText;
    css::uno::Reference<css::text::XText> xText(xShape, css::uno::UNO_QUERY);
    if (xText.is())
        sText = xText->getString();

    TextStyle aTextStyle{ DEFAULT_FONT_SIZE_INCHES, 0.0, 0.0, 0.0, 0.0, 0, 0 };
    try
    {
        float fCharHeight = 0.0f;
        if (xProperties.is()
            && (xProperties->getPropertyValue(u"CharHeight"_ustr) >>= fCharHeight)
            && fCharHeight > 0.0f)
        {
            aTextStyle.mfFontSizeInches = fCharHeight / 72.0;
        }

        sal_Int32 nCharColor = -1;
        if (xProperties.is()
            && (xProperties->getPropertyValue(u"CharColor"_ustr) >>= nCharColor)
            && nCharColor >= 0)
        {
            aTextStyle.maColor = colorToHexSimple(
                static_cast<sal_uInt32>(nCharColor));
        }

        sal_Int32 nMargin = 0;
        if (xProperties->getPropertyValue(u"TextLeftDistance"_ustr) >>= nMargin)
            aTextStyle.mfLeftMarginInches = nMargin * LO_TO_INCHES;
        if (xProperties->getPropertyValue(u"TextRightDistance"_ustr) >>= nMargin)
            aTextStyle.mfRightMarginInches = nMargin * LO_TO_INCHES;
        if (xProperties->getPropertyValue(u"TextUpperDistance"_ustr) >>= nMargin)
            aTextStyle.mfTopMarginInches = nMargin * LO_TO_INCHES;
        if (xProperties->getPropertyValue(u"TextLowerDistance"_ustr) >>= nMargin)
            aTextStyle.mfBottomMarginInches = nMargin * LO_TO_INCHES;

        sal_Int16 nParagraphAdjust
            = static_cast<sal_Int16>(css::style::ParagraphAdjust_LEFT);
        if (xProperties->getPropertyValue(u"ParaAdjust"_ustr) >>= nParagraphAdjust)
        {
            const auto eParagraphAdjust
                = static_cast<css::style::ParagraphAdjust>(nParagraphAdjust);
            if (eParagraphAdjust == css::style::ParagraphAdjust_CENTER)
                aTextStyle.mnHorizontalAlign = 1;
            else if (eParagraphAdjust == css::style::ParagraphAdjust_RIGHT)
                aTextStyle.mnHorizontalAlign = 2;
            else if (eParagraphAdjust == css::style::ParagraphAdjust_BLOCK)
                aTextStyle.mnHorizontalAlign = 3;
        }

        css::drawing::TextVerticalAdjust eVerticalAdjust
            = css::drawing::TextVerticalAdjust_TOP;
        if (xProperties->getPropertyValue(u"TextVerticalAdjust"_ustr)
            >>= eVerticalAdjust)
        {
            if (eVerticalAdjust == css::drawing::TextVerticalAdjust_CENTER)
                aTextStyle.mnVerticalAlign = 1;
            else if (eVerticalAdjust == css::drawing::TextVerticalAdjust_BOTTOM)
                aTextStyle.mnVerticalAlign = 2;
        }
    }
    catch (const css::uno::Exception&)
    {
    }

    if (xText.is())
        CollectTextRuns(xText, aTextStyle);

    // Select the VSDX geometry rows for this shape type.
    Geometry aGeometry;
    if (sServiceName == u"com.sun.star.drawing.EllipseShape"_ustr)
    {
        aGeometry = MakeEllipseGeometry(fWidth, fHeight);
    }
    else if (sServiceName == u"com.sun.star.drawing.LineShape"_ustr
             || sServiceName == u"com.sun.star.drawing.PolyLineShape"_ustr
             || sServiceName == u"com.sun.star.drawing.PolyPolygonShape"_ustr)
    {
        aGeometry = MakePointsGeometry(
            getUntransformedPoints(xProperties), fWidth, fHeight,
            sServiceName == u"com.sun.star.drawing.PolyPolygonShape"_ustr);
        if (aGeometry.empty())
            aGeometry = MakeRectangleGeometry();
    }
    else if (sServiceName == u"com.sun.star.drawing.OpenBezierShape"_ustr
             || sServiceName == u"com.sun.star.drawing.ClosedBezierShape"_ustr)
    {
        // Geometry is the untransformed, shape-local Bezier path.
        aGeometry = MakeBezierGeometry(
            getBezierPoints(xProperties, u"Geometry"_ustr), fWidth, fHeight,
            sServiceName == u"com.sun.star.drawing.ClosedBezierShape"_ustr,
            css::awt::Point());
        if (aGeometry.empty())
            aGeometry = MakeRectangleGeometry();
    }
    else if (sServiceName == u"com.sun.star.drawing.ConnectorShape"_ustr)
    {
        // PolyPolygonBezier is the connector's computed route, including all
        // routing bends and curve controls.  Connector coordinates are page
        // coordinates, unlike the local Geometry property of polygon shapes.
        aGeometry = MakeBezierGeometry(
            getBezierPoints(xProperties, u"PolyPolygonBezier"_ustr),
            fWidth, fHeight, false, aPos);
        if (aGeometry.empty())
            aGeometry = MakeRectangleGeometry();
    }
    else
    {
        aGeometry = MakeRectangleGeometry();
    }

    WriteRectangleToBuilder(rBuilder, sType, fPinX, fPinY, fWidth, fHeight,
                            fAngleRad, sFillColor, bNoFill, fFillTransparency,
                            sLineColor, fLineWidthInches, bLineVisible, sText,
                            aTextStyle, aGeometry, nBeginType, nBeginSize,
                            nEndType, nEndSize);
}

bool VisioExport::WriteTableToBuilder(
    OUStringBuffer& rBuilder,
    const css::uno::Reference<css::drawing::XShape>& xShape,
    double fPageHeight)
{
    try
    {
        css::uno::Reference<css::beans::XPropertySet> xShapeProperties(
            xShape, css::uno::UNO_QUERY_THROW);
        css::uno::Reference<css::table::XTable> xTable;
        xShapeProperties->getPropertyValue(u"Model"_ustr) >>= xTable;
        if (!xTable.is())
            return false;

        css::uno::Reference<css::container::XIndexAccess> xColumns(
            xTable->getColumns(), css::uno::UNO_QUERY_THROW);
        css::uno::Reference<css::container::XIndexAccess> xRows(
            xTable->getRows(), css::uno::UNO_QUERY_THROW);
        if (xColumns->getCount() == 0 || xRows->getCount() == 0)
            return false;

        std::vector<sal_Int32> aColumnWidths;
        aColumnWidths.reserve(xColumns->getCount());
        for (sal_Int32 nColumn = 0; nColumn < xColumns->getCount(); ++nColumn)
        {
            css::uno::Reference<css::beans::XPropertySet> xColumnProperties(
                xColumns->getByIndex(nColumn), css::uno::UNO_QUERY_THROW);
            sal_Int32 nWidth = 0;
            xColumnProperties->getPropertyValue(u"Width"_ustr) >>= nWidth;
            aColumnWidths.push_back(nWidth);
        }

        const css::awt::Point aTablePosition = xShape->getPosition();
        sal_Int32 nYOffset = 0;
        for (sal_Int32 nRow = 0; nRow < xRows->getCount(); ++nRow)
        {
            css::uno::Reference<css::beans::XPropertySet> xRowProperties(
                xRows->getByIndex(nRow), css::uno::UNO_QUERY_THROW);
            sal_Int32 nRowHeight = 0;
            xRowProperties->getPropertyValue(u"Height"_ustr) >>= nRowHeight;

            sal_Int32 nXOffset = 0;
            for (sal_Int32 nColumn = 0; nColumn < xColumns->getCount(); ++nColumn)
            {
                const sal_Int32 nColumnWidth = aColumnWidths[nColumn];
                auto xCell = xTable->getCellByPosition(nColumn, nRow);
                css::uno::Reference<css::beans::XPropertySet> xCellProperties(
                    xCell, css::uno::UNO_QUERY);

                OUString sFillColor = u"#ffffff"_ustr;
                bool bNoFill = false;
                double fCellFillTransparency = 0.0;
                if (xCellProperties.is())
                {
                    css::drawing::FillStyle eFillStyle = css::drawing::FillStyle_SOLID;
                    xCellProperties->getPropertyValue(u"FillStyle"_ustr) >>= eFillStyle;
                    bNoFill = eFillStyle == css::drawing::FillStyle_NONE;
                    if (!bNoFill)
                    {
                        sal_Int32 nFillColor = 0xffffff;
                        xCellProperties->getPropertyValue(u"FillColor"_ustr) >>= nFillColor;
                        sFillColor = colorToHexSimple(
                            static_cast<sal_uInt32>(nFillColor));
                        fCellFillTransparency =
                            fillTransparencyToFraction(xCellProperties);
                    }
                }

                OUString sText;
                TextStyle aTextStyle{ DEFAULT_FONT_SIZE_INCHES, 0.0, 0.0,
                                      0.0, 0.0, 1, 1 };
                css::uno::Reference<css::text::XText> xCellText(
                    xCell, css::uno::UNO_QUERY);
                if (xCellText.is())
                    sText = xCellText->getString();
                if (xCellProperties.is())
                {
                    float fCharHeight = 0.0f;
                    if ((xCellProperties->getPropertyValue(u"CharHeight"_ustr)
                         >>= fCharHeight)
                        && fCharHeight > 0.0f)
                    {
                        aTextStyle.mfFontSizeInches = fCharHeight / 72.0;
                    }
                    sal_Int32 nCharColor = -1;
                    if ((xCellProperties->getPropertyValue(u"CharColor"_ustr)
                         >>= nCharColor)
                        && nCharColor >= 0)
                    {
                        aTextStyle.maColor = colorToHexSimple(
                            static_cast<sal_uInt32>(nCharColor));
                    }
                }
                if (xCellText.is())
                    CollectTextRuns(xCellText, aTextStyle);

                const double fWidth = nColumnWidth * LO_TO_INCHES;
                const double fHeight = nRowHeight * LO_TO_INCHES;
                const double fPinX
                    = (aTablePosition.X + nXOffset + nColumnWidth / 2.0)
                      * LO_TO_INCHES;
                const double fPinY
                    = fPageHeight
                      - (aTablePosition.Y + nYOffset + nRowHeight / 2.0)
                            * LO_TO_INCHES;

                // Draw cells as ordinary Visio rectangles. This preserves the
                // table grid in applications that do not understand LO tables.
                WriteRectangleToBuilder(
                    rBuilder, u"Shape"_ustr, fPinX, fPinY, fWidth, fHeight, 0.0,
                    sFillColor, bNoFill, fCellFillTransparency, u"#ffffff"_ustr,
                    DEFAULT_LINE_WIDTH_INCHES, true, sText,
                    aTextStyle, MakeRectangleGeometry(), 0, 2, 0, 2);
                nXOffset += nColumnWidth;
            }
            nYOffset += nRowHeight;
        }
        return true;
    }
    catch (const css::uno::Exception&)
    {
        return false;
    }
}

void VisioExport::WriteRectangleToBuilder(
    OUStringBuffer& rBuilder, const OUString& rType, double fPinX,
    double fPinY, double fWidth, double fHeight, double fAngleRad,
    const OUString& rFillColor, bool bNoFill, double fFillTransparency,
    const OUString& rLineColor, double fLineWidthInches, bool bLineVisible,
    const OUString& rText, const TextStyle& rTextStyle, const Geometry& rGeometry,
    sal_Int32 nBeginType, sal_Int32 nBeginSize, sal_Int32 nEndType,
    sal_Int32 nEndSize)
{
    rBuilder.append(u"<Shape ID='");
    rBuilder.append(OUString::number(mnNextShapeId++));
    rBuilder.append(u"' Type='");
    rBuilder.append(rType);
    rBuilder.append(u"' LineStyle='0' FillStyle='0' TextStyle='0'>");

    // Position and size cells
    rBuilder.append(u"<Cell N='PinX' V='");
    rBuilder.append(fmtDouble(fPinX));
    rBuilder.append(u"'/>");
    rBuilder.append(u"<Cell N='PinY' V='");
    rBuilder.append(fmtDouble(fPinY));
    rBuilder.append(u"'/>");
    rBuilder.append(u"<Cell N='Width' V='");
    rBuilder.append(fmtDouble(fWidth));
    rBuilder.append(u"'/>");
    rBuilder.append(u"<Cell N='Height' V='");
    rBuilder.append(fmtDouble(fHeight));
    rBuilder.append(u"'/>");
    rBuilder.append(u"<Cell N='LocPinX' V='");
    rBuilder.append(fmtDouble(fWidth / 2.0));
    rBuilder.append(u"' F='Width*0.5'/>");
    rBuilder.append(u"<Cell N='LocPinY' V='");
    rBuilder.append(fmtDouble(fHeight / 2.0));
    rBuilder.append(u"' F='Height*0.5'/>");
    rBuilder.append(u"<Cell N='Angle' V='");
    rBuilder.append(fmtDouble(fAngleRad));
    rBuilder.append(u"'/>");
    rBuilder.append(u"<Cell N='FlipX' V='0'/>");
    rBuilder.append(u"<Cell N='FlipY' V='0'/>");
    rBuilder.append(u"<Cell N='ResizeMode' V='0'/>");

    // VSDX stores line weights in inches; U='PT' is only the display unit.
    if (bLineVisible)
    {
        rBuilder.append(u"<Cell N='LineWeight' V='");
        rBuilder.append(fmtDouble(fLineWidthInches));
        rBuilder.append(u"' U='PT' F='Inh'/>");
        rBuilder.append(u"<Cell N='LineColor' V='");
        rBuilder.append(rLineColor);
        rBuilder.append(u"' F='Inh'/>");
        rBuilder.append(u"<Cell N='BeginArrow' V='");
        rBuilder.append(OUString::number(nBeginType));
        rBuilder.append(u"'/>");
        rBuilder.append(u"<Cell N='BeginArrowSize' V='");
        rBuilder.append(OUString::number(nBeginSize));
        rBuilder.append(u"'/>");
        rBuilder.append(u"<Cell N='EndArrow' V='");
        rBuilder.append(OUString::number(nEndType));
        rBuilder.append(u"'/>");
        rBuilder.append(u"<Cell N='EndArrowSize' V='");
        rBuilder.append(OUString::number(nEndSize));
        rBuilder.append(u"'/>");
    }
    else
    {
        rBuilder.append(u"<Cell N='LinePattern' V='0' F='Inh'/>");
    }

    // Fill properties
    if (bNoFill)
    {
        rBuilder.append(u"<Cell N='FillPattern' V='0' F='Inh'/>");
    }
    else
    {
        rBuilder.append(u"<Cell N='FillForegnd' V='");
        rBuilder.append(rFillColor);
        rBuilder.append(u"' F='Inh'/>");
        rBuilder.append(u"<Cell N='FillBkgnd' V='#ffffff' F='Inh'/>");
        // Zero is the default style value, so the cells are only written
        // when the fill is actually translucent.
        if (fFillTransparency > 0.0)
        {
            rBuilder.append(u"<Cell N='FillForegndTrans' V='");
            rBuilder.append(fmtDouble(fFillTransparency));
            rBuilder.append(u"'/>");
            rBuilder.append(u"<Cell N='FillBkgndTrans' V='");
            rBuilder.append(fmtDouble(fFillTransparency));
            rBuilder.append(u"'/>");
        }
    }

    // Geometry
    rBuilder.append(u"<Section N='Geometry' IX='0'>");
    rBuilder.append(u"<Cell N='NoFill' V='0'/>");
    rBuilder.append(u"<Cell N='NoLine' V='0'/>");
    rBuilder.append(u"<Cell N='NoShow' V='0'/>");
    rBuilder.append(u"<Cell N='NoSnap' V='0'/>");
    rBuilder.append(u"<Cell N='NoQuickDrag' V='0'/>");
    sal_uInt32 nRowIx = 1;
    for (const auto& rRow : rGeometry)
    {
        rBuilder.append(u"<Row T='");
        rBuilder.append(rRow.aRowType);
        rBuilder.append(u"' IX='");
        rBuilder.append(OUString::number(nRowIx++));
        rBuilder.append(u"'>");
        for (const auto& rCell : rRow.aCells)
        {
            rBuilder.append(u"<Cell N='");
            rBuilder.append(rCell.aName);
            rBuilder.append(u"' V='");
            rBuilder.append(rCell.aValue);
            rBuilder.append(u"'/>");
        }
        rBuilder.append(u"</Row>");
    }
    rBuilder.append(u"</Section>");

    // Text
    if (!rText.isEmpty())
    {
        rBuilder.append(u"<Cell N='LeftMargin' V='");
        rBuilder.append(fmtDouble(rTextStyle.mfLeftMarginInches));
        rBuilder.append(u"'/>");
        rBuilder.append(u"<Cell N='RightMargin' V='");
        rBuilder.append(fmtDouble(rTextStyle.mfRightMarginInches));
        rBuilder.append(u"'/>");
        rBuilder.append(u"<Cell N='TopMargin' V='");
        rBuilder.append(fmtDouble(rTextStyle.mfTopMarginInches));
        rBuilder.append(u"'/>");
        rBuilder.append(u"<Cell N='BottomMargin' V='");
        rBuilder.append(fmtDouble(rTextStyle.mfBottomMarginInches));
        rBuilder.append(u"'/>");
        rBuilder.append(u"<Cell N='VerticalAlign' V='");
        rBuilder.append(OUString::number(rTextStyle.mnVerticalAlign));
        rBuilder.append(u"'/>");

        rBuilder.append(u"<Section N='Character'>");
        const size_t nCharacterRows
            = rTextStyle.maRuns.empty() ? 1 : rTextStyle.maRuns.size();
        for (size_t nRun = 0; nRun < nCharacterRows; ++nRun)
        {
            const TextRun* pRun
                = rTextStyle.maRuns.empty() ? nullptr : &rTextStyle.maRuns[nRun];
            rBuilder.append(u"<Row IX='");
            rBuilder.append(OUString::number(nRun));
            rBuilder.append(u"'><Cell N='Font' V='");
            rBuilder.append(escapeXml(pRun ? pRun->maFontName : u"Calibri"_ustr));
            rBuilder.append(u"'/><Cell N='Color' V='");
            rBuilder.append(pRun ? pRun->maColor : rTextStyle.maColor);
            rBuilder.append(u"'/><Cell N='Style' V='");
            rBuilder.append(OUString::number(pRun ? pRun->mnStyle : 0));
            rBuilder.append(u"'/><Cell N='Size' V='");
            rBuilder.append(fmtDouble(
                pRun ? pRun->mfFontSizeInches : rTextStyle.mfFontSizeInches));
            rBuilder.append(u"'/></Row>");
        }
        rBuilder.append(u"</Section>");
        rBuilder.append(u"<Section N='Paragraph'>");
        const size_t nParagraphRows
            = rTextStyle.maParagraphs.empty() ? 1 : rTextStyle.maParagraphs.size();
        for (size_t nParagraph = 0; nParagraph < nParagraphRows; ++nParagraph)
        {
            const sal_Int32 nHorizontalAlign
                = rTextStyle.maParagraphs.empty()
                      ? rTextStyle.mnHorizontalAlign
                      : rTextStyle.maParagraphs[nParagraph].mnHorizontalAlign;
            const double fLineSpacing
                = rTextStyle.maParagraphs.empty()
                      ? -1.2
                      : rTextStyle.maParagraphs[nParagraph].mfLineSpacing;
            rBuilder.append(u"<Row IX='");
            rBuilder.append(OUString::number(nParagraph));
            rBuilder.append(u"'><Cell N='IndFirst' V='0'/>");
            rBuilder.append(u"<Cell N='IndLeft' V='0'/>");
            rBuilder.append(u"<Cell N='IndRight' V='0'/>");
            rBuilder.append(u"<Cell N='SpLine' V='");
            rBuilder.append(fmtDouble(fLineSpacing));
            rBuilder.append(u"'/>");
            rBuilder.append(u"<Cell N='SpBefore' V='0'/>");
            rBuilder.append(u"<Cell N='SpAfter' V='0'/>");
            rBuilder.append(u"<Cell N='HorzAlign' V='");
            rBuilder.append(OUString::number(nHorizontalAlign));
            rBuilder.append(u"'/><Cell N='Bullet' V='0'/>");
            rBuilder.append(u"<Cell N='Flags' V='0'/></Row>");
        }
        rBuilder.append(u"</Section>");

        rBuilder.append(u"<Text>");
        if (rTextStyle.maRuns.empty())
        {
            rBuilder.append(escapeXml(rText));
        }
        else
        {
            for (size_t nRun = 0; nRun < rTextStyle.maRuns.size(); ++nRun)
            {
                const TextRun& rRun = rTextStyle.maRuns[nRun];
                if (nRun != 0 && rRun.mbParagraphStart)
                    rBuilder.append(u"\n");
                rBuilder.append(u"<cp IX='");
                rBuilder.append(OUString::number(nRun));
                rBuilder.append(u"'/>");
                if (rRun.mbParagraphStart)
                {
                    rBuilder.append(u"<pp IX='");
                    rBuilder.append(OUString::number(rRun.mnParagraphIndex));
                    rBuilder.append(u"'/>");
                }
                rBuilder.append(escapeXml(rRun.maText));
            }
        }
        rBuilder.append(u"</Text>");
    }

    rBuilder.append(u"</Shape>");
}

VisioExport::Geometry VisioExport::MakeRectangleGeometry() const
{
    // Rectangle path in relative (0..1) shape coordinates, closed.
    return {
        { u"RelMoveTo"_ustr, { { u"X"_ustr, u"0"_ustr }, { u"Y"_ustr, u"0"_ustr } } },
        { u"RelLineTo"_ustr, { { u"X"_ustr, u"1"_ustr }, { u"Y"_ustr, u"0"_ustr } } },
        { u"RelLineTo"_ustr, { { u"X"_ustr, u"1"_ustr }, { u"Y"_ustr, u"1"_ustr } } },
        { u"RelLineTo"_ustr, { { u"X"_ustr, u"0"_ustr }, { u"Y"_ustr, u"1"_ustr } } },
        { u"RelLineTo"_ustr, { { u"X"_ustr, u"0"_ustr }, { u"Y"_ustr, u"0"_ustr } } },
    };
}

VisioExport::Geometry VisioExport::MakeEllipseGeometry(double fWidthInches,
                                                       double fHeightInches) const
{
    // MS-VSDX 2.1.2.14: a single Ellipse row defines the whole ellipse. X/Y is
    // the center, (A,B) and (C,D) are two further points of the ellipse, all in
    // local shape units (inches, bottom-left origin). A Geometry section with
    // an Ellipse row has no other rows.
    return {
        { u"Ellipse"_ustr,
          { { u"X"_ustr, fmtDouble(fWidthInches / 2.0) },
            { u"Y"_ustr, fmtDouble(fHeightInches / 2.0) },
            { u"A"_ustr, fmtDouble(fWidthInches) },
            { u"B"_ustr, fmtDouble(fHeightInches / 2.0) },
            { u"C"_ustr, fmtDouble(fWidthInches / 2.0) },
            { u"D"_ustr, fmtDouble(0.0) } } },
    };
}

VisioExport::Geometry
VisioExport::MakePointsGeometry(
    const css::uno::Sequence<css::uno::Sequence<css::awt::Point>>& rPointSequences,
    double fWidthInches, double fHeightInches, bool bCloseSubpaths) const
{
    // Convert the untransformed LO points (1/100 mm, top-left origin) to
    // relative (0..1) VSDX shape coordinates (bottom-left origin): the local
    // Y axis is flipped, the global page PinY conversion is separate.
    Geometry aGeometry;
    for (const auto& rPoints : rPointSequences)
    {
        const sal_Int32 nCount = rPoints.getLength();
        if (nCount < 2)
            continue;
        for (sal_Int32 i = 0; i < nCount; ++i)
        {
            const css::awt::Point aPoint = rPoints[i];
            const double fRelX
                = fWidthInches > 0.0 ? (aPoint.X * LO_TO_INCHES) / fWidthInches : 0.5;
            const double fRelY
                = fHeightInches > 0.0 ? 1.0 - (aPoint.Y * LO_TO_INCHES) / fHeightInches : 0.5;
            aGeometry.push_back(
                { i == 0 ? u"RelMoveTo"_ustr : u"RelLineTo"_ustr,
                  { { u"X"_ustr, fmtDouble(fRelX) }, { u"Y"_ustr, fmtDouble(fRelY) } } });
        }
        if (bCloseSubpaths)
        {
            const bool bAlreadyClosed
                = rPoints[nCount - 1].X == rPoints[0].X
                  && rPoints[nCount - 1].Y == rPoints[0].Y;
            if (!bAlreadyClosed)
            {
                const css::awt::Point aFirst = rPoints[0];
                const double fRelX
                    = fWidthInches > 0.0 ? (aFirst.X * LO_TO_INCHES) / fWidthInches : 0.5;
                const double fRelY
                    = fHeightInches > 0.0
                          ? 1.0 - (aFirst.Y * LO_TO_INCHES) / fHeightInches
                          : 0.5;
                aGeometry.push_back(
                    { u"RelLineTo"_ustr,
                      { { u"X"_ustr, fmtDouble(fRelX) }, { u"Y"_ustr, fmtDouble(fRelY) } } });
            }
        }
    }
    return aGeometry;
}

VisioExport::Geometry VisioExport::MakeBezierGeometry(
    const css::drawing::PolyPolygonBezierCoords& rBezier,
    double fWidthInches, double fHeightInches, bool bCloseSubpaths,
    const css::awt::Point& rCoordinateOrigin) const
{
    // PolyPolygonBezierCoords encodes a cubic segment as two CONTROL points
    // followed by its endpoint.  RelCubBezTo uses the same two controls and
    // endpoint, with every coordinate relative to the shape width/height.
    Geometry aGeometry;
    const sal_Int32 nSequenceCount = rBezier.Coordinates.getLength();
    for (sal_Int32 nSequence = 0; nSequence < nSequenceCount; ++nSequence)
    {
        const auto& rPoints = rBezier.Coordinates[nSequence];
        const sal_Int32 nPointCount = rPoints.getLength();
        if (nPointCount < 2)
            continue;

        const bool bHaveFlags = nSequence < rBezier.Flags.getLength()
                                && rBezier.Flags[nSequence].getLength() == nPointCount;
        const auto toRelative = [&](const css::awt::Point& rPoint) {
            const double fLocalX = (rPoint.X - rCoordinateOrigin.X) * LO_TO_INCHES;
            const double fLocalY = (rPoint.Y - rCoordinateOrigin.Y) * LO_TO_INCHES;
            return std::pair<double, double>(
                fWidthInches > 0.0 ? fLocalX / fWidthInches : 0.5,
                fHeightInches > 0.0 ? 1.0 - fLocalY / fHeightInches : 0.5);
        };

        const auto [fStartX, fStartY] = toRelative(rPoints[0]);
        aGeometry.push_back(
            { u"RelMoveTo"_ustr,
              { { u"X"_ustr, fmtDouble(fStartX) },
                { u"Y"_ustr, fmtDouble(fStartY) } } });

        sal_Int32 nPoint = 1;
        while (nPoint < nPointCount)
        {
            const bool bCubic
                = bHaveFlags && nPoint + 2 < nPointCount
                  && rBezier.Flags[nSequence][nPoint]
                         == css::drawing::PolygonFlags_CONTROL
                  && rBezier.Flags[nSequence][nPoint + 1]
                         == css::drawing::PolygonFlags_CONTROL
                  && rBezier.Flags[nSequence][nPoint + 2]
                         != css::drawing::PolygonFlags_CONTROL;
            if (bCubic)
            {
                const auto [fControl1X, fControl1Y] = toRelative(rPoints[nPoint]);
                const auto [fControl2X, fControl2Y] = toRelative(rPoints[nPoint + 1]);
                const auto [fEndX, fEndY] = toRelative(rPoints[nPoint + 2]);
                aGeometry.push_back(
                    { u"RelCubBezTo"_ustr,
                      { { u"X"_ustr, fmtDouble(fEndX) },
                        { u"Y"_ustr, fmtDouble(fEndY) },
                        { u"A"_ustr, fmtDouble(fControl1X) },
                        { u"B"_ustr, fmtDouble(fControl1Y) },
                        { u"C"_ustr, fmtDouble(fControl2X) },
                        { u"D"_ustr, fmtDouble(fControl2Y) } } });
                nPoint += 3;
                continue;
            }

            // A well-formed cubic consumes its control points above.  Ignore a
            // stray control flag rather than drawing a visible kink through it.
            if (bHaveFlags
                && rBezier.Flags[nSequence][nPoint]
                       == css::drawing::PolygonFlags_CONTROL)
            {
                ++nPoint;
                continue;
            }

            const auto [fRelX, fRelY] = toRelative(rPoints[nPoint]);
            aGeometry.push_back(
                { u"RelLineTo"_ustr,
                  { { u"X"_ustr, fmtDouble(fRelX) },
                    { u"Y"_ustr, fmtDouble(fRelY) } } });
            ++nPoint;
        }

        if (bCloseSubpaths)
        {
            const bool bAlreadyClosed
                = rPoints[nPointCount - 1].X == rPoints[0].X
                  && rPoints[nPointCount - 1].Y == rPoints[0].Y;
            if (!bAlreadyClosed)
            {
                aGeometry.push_back(
                    { u"RelLineTo"_ustr,
                      { { u"X"_ustr, fmtDouble(fStartX) },
                        { u"Y"_ustr, fmtDouble(fStartY) } } });
            }
        }
    }
    return aGeometry;
}

} // namespace oox::core

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
css_comp_Draw_VisioExportFilter(
    css::uno::XComponentContext* pContext,
    const css::uno::Sequence<css::uno::Any>& rArguments)
{
    return cppu::acquire(new oox::core::VisioExport(pContext, rArguments));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
