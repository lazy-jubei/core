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
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XDrawPages.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/drawing/LineStyle.hpp>
#include <com/sun/star/drawing/LineDash.hpp>
#include <com/sun/star/drawing/TextVerticalAdjust.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/text/XText.hpp>
#include <com/sun/star/container/XIndexAccess.hpp>
#include <com/sun/star/table/XTable.hpp>
#include <com/sun/star/style/ParagraphAdjust.hpp>

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
    // Export-only filter
    return false;
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

void VisioExport::WriteShapeToBuilder(OUStringBuffer& rBuilder,
                                       const css::uno::Reference<css::drawing::XShape>& xShape,
                                       double fPageHeight)
{
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
    const double fAngleRad
        = nRotationHundredths * std::numbers::pi / 18000.0;

    // Match the dispatch used by LibreOffice's existing shape exporters.
    const OUString sServiceName = xShape->getShapeType();

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

        sal_Int32 nMargin = 0;
        if (xProperties->getPropertyValue(u"TextLeftDistance"_ustr) >>= nMargin)
            aTextStyle.mfLeftMarginInches = nMargin * LO_TO_INCHES;
        if (xProperties->getPropertyValue(u"TextRightDistance"_ustr) >>= nMargin)
            aTextStyle.mfRightMarginInches = nMargin * LO_TO_INCHES;
        if (xProperties->getPropertyValue(u"TextUpperDistance"_ustr) >>= nMargin)
            aTextStyle.mfTopMarginInches = nMargin * LO_TO_INCHES;
        if (xProperties->getPropertyValue(u"TextLowerDistance"_ustr) >>= nMargin)
            aTextStyle.mfBottomMarginInches = nMargin * LO_TO_INCHES;

        css::style::ParagraphAdjust eParagraphAdjust
            = css::style::ParagraphAdjust_LEFT;
        if (xProperties->getPropertyValue(u"ParaAdjust"_ustr) >>= eParagraphAdjust)
        {
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

    WriteRectangleToBuilder(rBuilder, sType, fPinX, fPinY, fWidth, fHeight,
                            fAngleRad, sFillColor, bNoFill, sLineColor,
                            fLineWidthInches, bLineVisible, sText,
                            aTextStyle);
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
                }

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
                    sFillColor, bNoFill, u"#ffffff"_ustr,
                    DEFAULT_LINE_WIDTH_INCHES, true, sText,
                    aTextStyle);
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
    const OUString& rFillColor, bool bNoFill, const OUString& rLineColor,
    double fLineWidthInches, bool bLineVisible, const OUString& rText,
    const TextStyle& rTextStyle)
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
    }

    // Geometry (default: rectangle for custom shapes, will need enhancement)
    rBuilder.append(u"<Section N='Geometry' IX='0'>");
    rBuilder.append(u"<Cell N='NoFill' V='0'/>");
    rBuilder.append(u"<Cell N='NoLine' V='0'/>");
    rBuilder.append(u"<Cell N='NoShow' V='0'/>");
    rBuilder.append(u"<Cell N='NoSnap' V='0'/>");
    rBuilder.append(u"<Cell N='NoQuickDrag' V='0'/>");
    // Default rectangle geometry (enhance with actual path data)
    rBuilder.append(u"<Row T='RelMoveTo' IX='1'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row>");
    rBuilder.append(u"<Row T='RelLineTo' IX='2'><Cell N='X' V='1'/><Cell N='Y' V='0'/></Row>");
    rBuilder.append(u"<Row T='RelLineTo' IX='3'><Cell N='X' V='1'/><Cell N='Y' V='1'/></Row>");
    rBuilder.append(u"<Row T='RelLineTo' IX='4'><Cell N='X' V='0'/><Cell N='Y' V='1'/></Row>");
    rBuilder.append(u"<Row T='RelLineTo' IX='5'><Cell N='X' V='0'/><Cell N='Y' V='0'/></Row>");
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

        rBuilder.append(u"<Section N='Character'><Row IX='0'>");
        rBuilder.append(u"<Cell N='Font' V='Calibri'/>");
        rBuilder.append(u"<Cell N='Color' V='#000000'/>");
        rBuilder.append(u"<Cell N='Style' V='0'/>");
        rBuilder.append(u"<Cell N='Size' V='");
        rBuilder.append(fmtDouble(rTextStyle.mfFontSizeInches));
        rBuilder.append(u"'/>");
        rBuilder.append(u"</Row></Section>");
        rBuilder.append(u"<Section N='Paragraph'><Row IX='0'>");
        rBuilder.append(u"<Cell N='IndFirst' V='0'/>");
        rBuilder.append(u"<Cell N='IndLeft' V='0'/>");
        rBuilder.append(u"<Cell N='IndRight' V='0'/>");
        rBuilder.append(u"<Cell N='SpLine' V='-1.2'/>");
        rBuilder.append(u"<Cell N='SpBefore' V='0'/>");
        rBuilder.append(u"<Cell N='SpAfter' V='0'/>");
        rBuilder.append(u"<Cell N='HorzAlign' V='");
        rBuilder.append(OUString::number(rTextStyle.mnHorizontalAlign));
        rBuilder.append(u"'/>");
        rBuilder.append(u"<Cell N='Bullet' V='0'/>");
        rBuilder.append(u"<Cell N='Flags' V='0'/>");
        rBuilder.append(u"</Row></Section>");

        // Escape text for XML
        OUString sEscaped = rText;
        sEscaped = sEscaped.replaceAll(u"&"_ustr, u"&amp;"_ustr);
        sEscaped = sEscaped.replaceAll(u"<"_ustr, u"&lt;"_ustr);
        sEscaped = sEscaped.replaceAll(u">"_ustr, u"&gt;"_ustr);
        sEscaped = sEscaped.replaceAll(u"\""_ustr, u"&quot;"_ustr);

        rBuilder.append(u"<Text>");
        rBuilder.append(sEscaped);
        rBuilder.append(u"</Text>");
    }

    rBuilder.append(u"</Shape>");
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
