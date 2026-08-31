/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "sdmodeltestbase.hxx"

#include <com/sun/star/awt/Point.hpp>
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/drawing/XDrawPage.hpp>
#include <com/sun/star/drawing/XDrawPagesSupplier.hpp>
#include <com/sun/star/drawing/XShape.hpp>
#include <com/sun/star/drawing/XShapeGrouper.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/text/XText.hpp>
#include <comphelper/propertyvalue.hxx>

using namespace ::com::sun::star;

namespace
{

// Precedent: sd/qa/unit/misc-tests.cxx creates shapes via the document's
// XMultiServiceFactory (com.sun.star.lang), not via the page.
css::uno::Reference<drawing::XShape>
createShape(css::uno::Reference<lang::XComponent> const& xComponent,
            css::uno::Reference<drawing::XDrawPage> const& xPage,
            const OUString& rShapeServiceName)
{
    css::uno::Reference<lang::XMultiServiceFactory> xFactory(
        xComponent, css::uno::UNO_QUERY_THROW);
    css::uno::Reference<drawing::XShape> xNewShape(
        xFactory->createInstance(rShapeServiceName), css::uno::UNO_QUERY_THROW);
    css::uno::Reference<drawing::XShapes> xShapes(xPage, css::uno::UNO_QUERY_THROW);
    xShapes->add(xNewShape);
    return xNewShape;
}

// Position and Size are XShape methods, not XPropertySet properties.
void setPosSize(css::uno::Reference<drawing::XShape> const& xShape, sal_Int32 nX,
                sal_Int32 nY, sal_Int32 nWidth, sal_Int32 nHeight)
{
    xShape->setPosition(awt::Point(nX, nY));
    xShape->setSize(awt::Size(nWidth, nHeight));
}

// XPath prefix of the Geometry section of the first exported shape. local-name()
// is used to stay independent of the default namespace of the VSDX page part.
const OString sGeomXPath = "//*[local-name()='Section' and @N='Geometry']";

} // namespace

class SdVisioExportTest : public SdModelTestBase
{
public:
    SdVisioExportTest()
        : SdModelTestBase("/sd/qa/unit/data/")
    {
    }

protected:
    void saveAsVisio()
    {
        css::uno::Reference<frame::XStorable> xStorable(mxComponent, css::uno::UNO_QUERY);
        CPPUNIT_ASSERT(xStorable.is());
        const css::uno::Sequence<beans::PropertyValue> aArgs{
            comphelper::makePropertyValue("FilterName", OUString("Visio VSDX")),
            comphelper::makePropertyValue("Overwrite", true),
        };
        CPPUNIT_ASSERT_NO_THROW(xStorable->storeToURL(maTempFile.GetURL(), aArgs));
    }

    xmlDocUniquePtr parsePage1()
    {
        return parseExport("visio/pages/page1.xml");
    }
};

CPPUNIT_TEST_FIXTURE(SdVisioExportTest, testEllipseGeometry)
{
    createSdDrawDoc();
    css::uno::Reference<drawing::XShape> xShape(
        createShape(mxComponent, getPage(0), "com.sun.star.drawing.EllipseShape"));
    // 1 inch x 0.5 inch ellipse
    setPosSize(xShape, 1000, 1000, 2540, 1270);

    saveAsVisio();
    xmlDocUniquePtr pXml = parsePage1();

    // MS-VSDX: one Ellipse row, no other rows. X/Y is the center, (A,B) and
    // (C,D) are two further points of the ellipse, in local inches.
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row']", 1);
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row'][1]", "T", u"Ellipse");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='X']",
                "V", u"0.5");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='Y']",
                "V", u"0.25");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='A']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='B']",
                "V", u"0.25");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='C']",
                "V", u"0.5");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='D']",
                "V", u"0");
}

CPPUNIT_TEST_FIXTURE(SdVisioExportTest, testCombinedFilterCanImportExportedVsdx)
{
    createSdDrawDoc();
    css::uno::Reference<drawing::XShape> xShape(
        createShape(mxComponent, getPage(0), "com.sun.star.drawing.RectangleShape"));
    setPosSize(xShape, 1000, 1000, 2540, 1270);

    saveAsVisio();
    dispose();

    const css::uno::Sequence<beans::PropertyValue> aArgs{
        comphelper::makePropertyValue("FilterName", OUString("Visio VSDX")),
    };
    loadFromURL(maTempFile.GetURL(), aArgs);

    css::uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(
        mxComponent, css::uno::UNO_QUERY_THROW);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xDrawPagesSupplier->getDrawPages()->getCount());
    css::uno::Reference<drawing::XShapes> xShapes(getPage(0), css::uno::UNO_QUERY_THROW);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xShapes->getCount());
}

CPPUNIT_TEST_FIXTURE(SdVisioExportTest, testGroupChildrenKeepFillAndText)
{
    createSdDrawDoc();
    css::uno::Reference<drawing::XDrawPage> xPage(getPage(0));

    css::uno::Reference<drawing::XShape> xRectangle(
        createShape(mxComponent, xPage, "com.sun.star.drawing.RectangleShape"));
    setPosSize(xRectangle, 1000, 1000, 4000, 2000);
    css::uno::Reference<beans::XPropertySet> xRectangleProperties(
        xRectangle, css::uno::UNO_QUERY_THROW);
    xRectangleProperties->setPropertyValue(u"FillColor"_ustr,
                                            css::uno::Any(sal_Int32(0x8db1e2)));

    css::uno::Reference<drawing::XShape> xTextShape(
        createShape(mxComponent, xPage, "com.sun.star.drawing.TextShape"));
    setPosSize(xTextShape, 1500, 1500, 3000, 1000);
    css::uno::Reference<text::XText> xText(xTextShape, css::uno::UNO_QUERY_THROW);
    xText->setString(u"Grouped label"_ustr);
    css::uno::Reference<beans::XPropertySet> xTextProperties(
        xTextShape, css::uno::UNO_QUERY_THROW);
    xTextProperties->setPropertyValue(u"CharColor"_ustr,
                                      css::uno::Any(sal_Int32(0xff0000)));

    css::uno::Reference<drawing::XShapeGrouper> xGrouper(
        xPage, css::uno::UNO_QUERY_THROW);
    css::uno::Reference<drawing::XShapes> xPageShapes(xPage, css::uno::UNO_QUERY_THROW);
    CPPUNIT_ASSERT(xGrouper->group(xPageShapes).is());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xPageShapes->getCount());

    saveAsVisio();
    xmlDocUniquePtr pXml = parsePage1();

    // A Draw group is a container.  Its two visual children must be exported,
    // rather than one blank rectangle standing in for the group.
    assertXPath(pXml, "//*[local-name()='Shapes']/*[local-name()='Shape']", 2);
    assertXPath(pXml,
                "//*[local-name()='Shape']/*[local-name()='Cell' and @N='FillForegnd' and @V='#8db1e2']",
                1);
    assertXPathContent(pXml, "//*[local-name()='Shape']/*[local-name()='Text']",
                       u"Grouped label");
    assertXPath(pXml,
                "//*[local-name()='Shape'][*[local-name()='Text' and text()='Grouped label']]"
                "/*[local-name()='Section' and @N='Character']/*[local-name()='Row']"
                "/*[local-name()='Cell' and @N='Color']",
                "V", u"#ff0000");
}

CPPUNIT_TEST_FIXTURE(SdVisioExportTest, testLineGeometry)
{
    createSdDrawDoc();
    css::uno::Reference<drawing::XShape> xShape(
        createShape(mxComponent, getPage(0), "com.sun.star.drawing.LineShape"));
    // Default line runs across the bounding box diagonal
    setPosSize(xShape, 1000, 1000, 2540, 1270);

    saveAsVisio();
    xmlDocUniquePtr pXml = parsePage1();

    // Open path: one RelMoveTo and one RelLineTo, no closing row.
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row']", 2);
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row'][1]", "T", u"RelMoveTo");
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row'][2]", "T", u"RelLineTo");
    // Diagonal corners in relative coordinates with the Visio Y axis (bottom-left
    // origin): LO top-left (0,0) -> (0,1), LO bottom-right (w,h) -> (1,0).
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='X']",
                "V", u"0");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='Y']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][2]/*[local-name()='Cell' and @N='X']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][2]/*[local-name()='Cell' and @N='Y']",
                "V", u"0");
}

CPPUNIT_TEST_FIXTURE(SdVisioExportTest, testPolygonGeometry)
{
    createSdDrawDoc();
    css::uno::Reference<drawing::XShape> xShape(
        createShape(mxComponent, getPage(0), "com.sun.star.drawing.PolyPolygonShape"));
    // Right triangle in a 20mm x 20mm box, points in 1/100 mm, top-left origin
    setPosSize(xShape, 0, 0, 2000, 2000);
    css::uno::Sequence<css::uno::Sequence<awt::Point>> aPolygons
        = { css::uno::Sequence<awt::Point>({ awt::Point(0, 0), awt::Point(2000, 0),
                                             awt::Point(0, 2000) }) };
    css::uno::Reference<beans::XPropertySet> xShapeProperties(
        xShape, css::uno::UNO_QUERY_THROW);
    xShapeProperties->setPropertyValue("PolyPolygon", css::uno::Any(aPolygons));

    saveAsVisio();
    xmlDocUniquePtr pXml = parsePage1();

    // Closed path: RelMoveTo + RelLineTo per vertex + one closing RelLineTo.
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row']", 4);
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row'][1]", "T", u"RelMoveTo");
    for (int nRow = 2; nRow <= 4; ++nRow)
    {
        const OString sRowXPath
            = sGeomXPath + "/*[local-name()='Row'][" + OString::number(nRow) + "]";
        assertXPath(pXml, sRowXPath, "T", u"RelLineTo");
    }
    // LO (0,0) -> (0,1), LO (2000,0) -> (1,1), LO (0,2000) -> (0,0), close -> (0,1)
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='X']",
                "V", u"0");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='Y']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][2]/*[local-name()='Cell' and @N='X']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][2]/*[local-name()='Cell' and @N='Y']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][3]/*[local-name()='Cell' and @N='X']",
                "V", u"0");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][3]/*[local-name()='Cell' and @N='Y']",
                "V", u"0");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][4]/*[local-name()='Cell' and @N='X']",
                "V", u"0");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][4]/*[local-name()='Cell' and @N='Y']",
                "V", u"1");
}

CPPUNIT_TEST_FIXTURE(SdVisioExportTest, testPolyLineGeometry)
{
    createSdDrawDoc();
    css::uno::Reference<drawing::XShape> xShape(
        createShape(mxComponent, getPage(0), "com.sun.star.drawing.PolyLineShape"));
    // L-shaped open path in a 20mm x 20mm box, points in 1/100 mm. PolyLineShape
    // takes its points via the PolyPolygon property (PolyPolygonDescriptor).
    setPosSize(xShape, 0, 0, 2000, 2000);
    css::uno::Sequence<css::uno::Sequence<awt::Point>> aPolylines
        = { css::uno::Sequence<awt::Point>({ awt::Point(0, 0), awt::Point(2000, 0),
                                             awt::Point(2000, 2000) }) };
    css::uno::Reference<beans::XPropertySet> xShapeProperties(
        xShape, css::uno::UNO_QUERY_THROW);
    xShapeProperties->setPropertyValue("PolyPolygon", css::uno::Any(aPolylines));

    saveAsVisio();
    xmlDocUniquePtr pXml = parsePage1();

    // Open path: one RelMoveTo + one RelLineTo per further point, no closing row.
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row']", 3);
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row'][1]", "T", u"RelMoveTo");
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row'][2]", "T", u"RelLineTo");
    assertXPath(pXml, sGeomXPath + "/*[local-name()='Row'][3]", "T", u"RelLineTo");
    // LO (0,0) -> (0,1), LO (2000,0) -> (1,1), LO (2000,2000) -> (1,0)
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='X']",
                "V", u"0");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][1]/*[local-name()='Cell' and @N='Y']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][2]/*[local-name()='Cell' and @N='X']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][2]/*[local-name()='Cell' and @N='Y']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][3]/*[local-name()='Cell' and @N='X']",
                "V", u"1");
    assertXPath(pXml,
                sGeomXPath
                    + "/*[local-name()='Row'][3]/*[local-name()='Cell' and @N='Y']",
                "V", u"0");
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
