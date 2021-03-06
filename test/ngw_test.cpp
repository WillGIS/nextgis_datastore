/******************************************************************************
 * Project:  libngstore
 * Purpose:  NextGIS store and visualisation support library
 * Author: Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 ******************************************************************************
 *   Copyright (c) 2016-2020 NextGIS, <info@nextgis.com>
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Lesser General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include "test.h"

#include "ngstore/api.h"

TEST(NGWTests, TestResourceGroup) {
    initLib();

    // Create connection
    auto connection = createConnection("sandbox.nextgis.com");
    ASSERT_NE(connection, nullptr);

    // Create resource group
    time_t rawTime = std::time(nullptr);
    auto groupName = "ngstest_group_" + std::to_string(rawTime);

    auto group = createGroup(connection, groupName);
    ASSERT_NE(group, nullptr);

    // Rename resource group
    auto newName = groupName + "_2";
    EXPECT_EQ(ngsCatalogObjectRename(group, newName.c_str()), COD_SUCCESS);

    // Delete resource group
    EXPECT_EQ(ngsCatalogObjectDelete(group), COD_SUCCESS);

    // Delete connection
    EXPECT_EQ(ngsCatalogObjectDelete(connection), COD_SUCCESS);

    ngsUnInit();
}

TEST(NGWTests, TestVectorLayer) {
    initLib();

    // Create connection
    auto connection = createConnection("sandbox.nextgis.com");
    ASSERT_NE(connection, nullptr);

    // Create resource group
    time_t rawTime = std::time(nullptr);
    auto groupName = "ngstest_group_" + std::to_string(rawTime);

    auto group = createGroup(connection, groupName);
    ASSERT_NE(group, nullptr);

    // Create empty vector layer
    char** options = nullptr;
    options = ngsListAddNameIntValue(options, "TYPE", CAT_NGW_VECTOR_LAYER);
    options = ngsListAddNameValue(options, "DESCRIPTION", "некое описание");
    options = ngsListAddNameValue(options, "GEOMETRY_TYPE", "POINT");
    options = ngsListAddNameValue(options, "FIELD_COUNT", "5");
    options = ngsListAddNameValue(options, "FIELD_0_TYPE", "INTEGER");
    options = ngsListAddNameValue(options, "FIELD_0_NAME", "type");
    options = ngsListAddNameValue(options, "FIELD_0_ALIAS", "тип");
    options = ngsListAddNameValue(options, "FIELD_1_TYPE", "STRING");
    options = ngsListAddNameValue(options, "FIELD_1_NAME", "desc");
    options = ngsListAddNameValue(options, "FIELD_1_ALIAS", "описание");
    options = ngsListAddNameValue(options, "FIELD_2_TYPE", "REAL");
    options = ngsListAddNameValue(options, "FIELD_2_NAME", "val");
    options = ngsListAddNameValue(options, "FIELD_2_ALIAS", "плавающая точка");
    options = ngsListAddNameValue(options, "FIELD_3_TYPE", "DATE_TIME");
    options = ngsListAddNameValue(options, "FIELD_3_NAME", "date");
    options = ngsListAddNameValue(options, "FIELD_3_ALIAS", "Это дата");
    options = ngsListAddNameValue(options, "FIELD_4_TYPE", "STRING");
    options = ngsListAddNameValue(options, "FIELD_4_NAME", "невалидное имя");

    auto vectorLayer = ngsCatalogObjectCreate(group, "новый точечный слой", options);
    ngsListFree(options);

    ASSERT_NE(vectorLayer, nullptr);

    // Rename vector layer
    EXPECT_EQ(ngsCatalogObjectRename(vectorLayer, "новый точечный слой 2"), COD_SUCCESS);

    // Delete vector layer
    EXPECT_EQ(ngsCatalogObjectDelete(vectorLayer), COD_SUCCESS);

    // Delete resource group
    EXPECT_EQ(ngsCatalogObjectDelete(group), COD_SUCCESS);

    // Delete connection
    EXPECT_EQ(ngsCatalogObjectDelete(connection), COD_SUCCESS);

    ngsUnInit();
}

TEST(NGWTests, TestPaste) {
    initLib();

    // Create connection
    auto connection = createConnection("sandbox.nextgis.com");
    ASSERT_NE(connection, nullptr);

    // Create resource group
    time_t rawTime = std::time(nullptr);
    auto groupName = "ngstest_group_" + std::to_string(rawTime);
    auto group = createGroup(connection, groupName);
    ASSERT_NE(group, nullptr);

    // Paste vector layer
    resetCounter();
    char** options = nullptr;
    // Add descritpion to NGW vector layer
    options = ngsListAddNameValue(options, "DESCRIPTION", "описание тест1");
    // If source layer has mixed geometries (point + multipoints, lines +
    // multilines, etc.) create output vector layers with multi geometries.
    // Otherwise the layer for each type geometry form source layer will create.
    options = ngsListAddNameValue(options, "FORCE_GEOMETRY_TO_MULTI", "TRUE");
    // Skip empty geometries. Mandatory for NGW?
    options = ngsListAddNameValue(options, "SKIP_EMPTY_GEOMETRY", "TRUE");
    // Check if geometry valid. Non valid geometry will not add to destination
    // layer.
    options = ngsListAddNameValue(options, "SKIP_INVALID_GEOMETRY", "TRUE");
    const char *layerName = "новый слой 4";
    // Set new layer name. If not set, the source layer name will use.
    options = ngsListAddNameValue(options, "NEW_NAME", layerName);

    CatalogObjectH shape = getLocalFile("/data/railway-mini.zip/railway-mini.shp");
    EXPECT_EQ(ngsCatalogObjectCopy(shape, group, options,
                                   ngsTestProgressFunc, nullptr), COD_SUCCESS);

    EXPECT_GE(getCounter(), 470);

    // Find loaded layer by name
    auto vectorLayer = ngsCatalogObjectGetByName(group, layerName, 1);
    ASSERT_NE(vectorLayer, nullptr);

    EXPECT_GE(ngsFeatureClassCount(vectorLayer), 470);

    // Rename vector layer
    EXPECT_EQ(ngsCatalogObjectRename(vectorLayer, "новый слой 3"), COD_SUCCESS);

    // Delete vector layer
    EXPECT_EQ(ngsCatalogObjectDelete(vectorLayer), COD_SUCCESS);

    // Delete resource group
    EXPECT_EQ(ngsCatalogObjectDelete(group), COD_SUCCESS);

    // Delete connection
    EXPECT_EQ(ngsCatalogObjectDelete(connection), COD_SUCCESS);

    ngsUnInit();
}

TEST(NGWTests, TestPasteMI) {
    initLib();

    // Create connection
    auto connection = createConnection("sandbox.nextgis.com");
    ASSERT_NE(connection, nullptr);

    // Create resource group
    time_t rawTime = std::time(nullptr);
    auto groupName = "ngstest_group_" + std::to_string(rawTime);
    auto group = createGroup(connection, groupName);
    ASSERT_NE(group, nullptr);

    uploadMIToNGW("/data/bld.tab", "новый слой 4", group);

    // Find loaded layer by name
    auto vectorLayer = ngsCatalogObjectGetByName(group, "новый слой 4", 1);
    ASSERT_NE(vectorLayer, nullptr);

    EXPECT_GE(ngsFeatureClassCount(vectorLayer), 5);

    // Rename vector layer
    EXPECT_EQ(ngsCatalogObjectRename(vectorLayer, "новый слой 3"), COD_SUCCESS);

    // MapServer
    auto style = createStyle(vectorLayer, "новый стиль mapserver",
                             "test Mapserver style", CAT_NGW_MAPSERVER_STYLE,
                             "<map><layer><styleitem>OGR_STYLE</styleitem><class><name>default</name></class></layer></map>");
    ASSERT_NE(style, nullptr);

    // Delete vector layer
    EXPECT_EQ(ngsCatalogObjectDelete(vectorLayer), COD_SUCCESS);

    // Delete resource group
    EXPECT_EQ(ngsCatalogObjectDelete(group), COD_SUCCESS);

    // Delete connection
    EXPECT_EQ(ngsCatalogObjectDelete(connection), COD_SUCCESS);

    ngsUnInit();
}

TEST(NGWTests, TestAttachments) {
    initLib();

    // Create connection
    auto connection = createConnection("sandbox.nextgis.com");
    ASSERT_NE(connection, nullptr);

    // Create resource group
    time_t rawTime = std::time(nullptr);
    auto groupName = "ngstest_group_" + std::to_string(rawTime);
    auto group = createGroup(connection, groupName);
    ASSERT_NE(group, nullptr);

    uploadMIToNGW("/data/bld.tab", "новый слой 4", group);

    // Find loaded layer by name
    auto vectorLayer = ngsCatalogObjectGetByName(group, "новый слой 4", 1);
    ASSERT_NE(vectorLayer, nullptr);

    auto feature = ngsFeatureClassNextFeature(vectorLayer);
    ASSERT_NE(feature, nullptr);

    std::string testPath = ngsGetCurrentDirectory();
    std::string testAttachmentPath = ngsFormFileName(
                testPath.c_str(), "download.cmake", nullptr);
    long long id = ngsFeatureAttachmentAdd(
                feature, "test.txt", "test add attachment",
                testAttachmentPath.c_str(), nullptr, 0);
    EXPECT_NE(id, -1);

    EXPECT_EQ(ngsFeatureAttachmentUpdate(feature, id, "notest.txt",
                                      "test update attachment", 0), 1);

    EXPECT_EQ(ngsCatalogObjectSync(vectorLayer), 1);

    EXPECT_EQ(ngsFeatureAttachmentDelete(feature, id, 0), 1);

    // Delete vector layer
    EXPECT_EQ(ngsCatalogObjectDelete(vectorLayer), COD_SUCCESS);

    // Delete resource group
    EXPECT_EQ(ngsCatalogObjectDelete(group), COD_SUCCESS);

    // Delete connection
    EXPECT_EQ(ngsCatalogObjectDelete(connection), COD_SUCCESS);

    ngsUnInit();
}

TEST(NGWTests, TestCreateStyle) {
    initLib();
    // Create connection
    auto connection = createConnection("sandbox.nextgis.com");
    ASSERT_NE(connection, nullptr);

    // Create resource group
    time_t rawTime = std::time(nullptr);
    auto groupName = "ngstest_group_" + std::to_string(rawTime);
    auto group = createGroup(connection, groupName);
    ASSERT_NE(group, nullptr);

    uploadMIToNGW("/data/bld.tab", "новый слой 4", group);

    // Find loaded layer by name
    auto vectorLayer = ngsCatalogObjectGetByName(group, "новый слой 4", 1);
    ASSERT_NE(vectorLayer, nullptr);

    // MapServer
    auto style = createStyle(vectorLayer, "новый стиль mapserver",
                             "test Mapserver style", CAT_NGW_MAPSERVER_STYLE,
                             "<map><layer><styleitem>OGR_STYLE</styleitem><class><name>default</name></class></layer></map>");
    ASSERT_NE(style, nullptr);

    // QGIS
    style = createStyle(vectorLayer, "новый стиль qgis",
                        "test qgis style", CAT_NGW_QGISVECTOR_STYLE,
                        "<!DOCTYPE qgis PUBLIC 'http://mrcc.com/qgis.dtd' 'SYSTEM'> \
                        <qgis version=\"2.14.8-Essen\" minimumScale=\"-4.65661e-10\" maximumScale=\"1e+08\" simplifyDrawingHints=\"0\" minLabelScale=\"0\" maxLabelScale=\"1e+08\" simplifyDrawingTol=\"1\" simplifyMaxScale=\"1\" hasScaleBasedVisibilityFlag=\"0\" simplifyLocal=\"1\" scaleBasedLabelVisibilityFlag=\"0\"> \
                          <renderer-v2 forceraster=\"0\" symbollevels=\"0\" type=\"RuleRenderer\" enableorderby=\"0\"> \
                            <rules key=\"{16db2044-02b1-4be3-ac30-9fdea2ad010d}\"> \
                              <rule filter=\" &quot;visible&quot; = 0\" key=\"{4902320a-6a18-4318-a4c5-f5e7a033a3b0}\" symbol=\"0\" label=\"archive\"/> \
                            </rules> \
                            <symbols> \
                              <symbol alpha=\"1\" clip_to_extent=\"1\" type=\"marker\" name=\"0\"> \
                                <layer pass=\"0\" class=\"SimpleMarker\" locked=\"0\"> \
                                  <prop k=\"angle\" v=\"0\"/> \
                                  <prop k=\"color\" v=\"81,160,130,255\"/> \
                                  <prop k=\"horizontal_anchor_point\" v=\"1\"/> \
                                  <prop k=\"name\" v=\"circle\"/> \
                                  <prop k=\"offset\" v=\"0,0\"/> \
                                  <prop k=\"offset_map_unit_scale\" v=\"0,0,0,0,0,0\"/> \
                                  <prop k=\"offset_unit\" v=\"MM\"/> \
                                  <prop k=\"outline_color\" v=\"0,0,0,255\"/> \
                                  <prop k=\"outline_style\" v=\"solid\"/> \
                                  <prop k=\"outline_width\" v=\"0\"/> \
                                  <prop k=\"outline_width_map_unit_scale\" v=\"0,0,0,0,0,0\"/> \
                                  <prop k=\"outline_width_unit\" v=\"MM\"/> \
                                  <prop k=\"scale_method\" v=\"diameter\"/> \
                                  <prop k=\"size\" v=\"2\"/> \
                                  <prop k=\"size_map_unit_scale\" v=\"0,0,0,0,0,0\"/> \
                                  <prop k=\"size_unit\" v=\"MM\"/> \
                                  <prop k=\"vertical_anchor_point\" v=\"1\"/> \
                                </layer> \
                              </symbol> \
                            </symbols> \
                          </renderer-v2> \
                          <labeling type=\"simple\"/> \
                          <blendMode>0</blendMode> \
                          <featureBlendMode>0</featureBlendMode> \
                          <layerTransparency>0</layerTransparency> \
                          <displayfield>description</displayfield> \
                          <label>0</label> \
                          <labelattributes> \
                            <label fieldname=\"\" text=\"Label\"/> \
                            <family fieldname=\"\" name=\"Open Sans\"/> \
                            <size fieldname=\"\" units=\"pt\" value=\"12\"/> \
                            <bold fieldname=\"\" on=\"0\"/> \
                            <italic fieldname=\"\" on=\"0\"/> \
                            <underline fieldname=\"\" on=\"0\"/> \
                            <strikeout fieldname=\"\" on=\"0\"/> \
                            <color fieldname=\"\" red=\"0\" blue=\"0\" green=\"0\"/> \
                            <x fieldname=\"\"/> \
                            <y fieldname=\"\"/> \
                            <offset x=\"0\" y=\"0\" units=\"pt\" yfieldname=\"\" xfieldname=\"\"/> \
                            <angle fieldname=\"\" value=\"0\" auto=\"0\"/> \
                            <alignment fieldname=\"\" value=\"center\"/> \
                            <buffercolor fieldname=\"\" red=\"255\" blue=\"255\" green=\"255\"/> \
                            <buffersize fieldname=\"\" units=\"pt\" value=\"1\"/> \
                            <bufferenabled fieldname=\"\" on=\"\"/> \
                            <multilineenabled fieldname=\"\" on=\"\"/> \
                            <selectedonly on=\"\"/> \
                          </labelattributes> \
                          <SingleCategoryDiagramRenderer diagramType=\"Pie\"> \
                            <DiagramCategory penColor=\"#000000\" labelPlacementMethod=\"XHeight\" penWidth=\"0\" diagramOrientation=\"Up\" minimumSize=\"0\" barWidth=\"5\" penAlpha=\"255\" maxScaleDenominator=\"1e+08\" backgroundColor=\"#ffffff\" transparency=\"0\" width=\"15\" scaleDependency=\"Area\" backgroundAlpha=\"255\" angleOffset=\"1440\" scaleBasedVisibility=\"0\" enabled=\"0\" height=\"15\" sizeType=\"MM\" minScaleDenominator=\"-4.65661e-10\"> \
                              <fontProperties description=\"Open Sans,9,-1,5,50,0,0,0,0,0\" style=\"\"/> \
                            </DiagramCategory> \
                          </SingleCategoryDiagramRenderer> \
                          <DiagramLayerSettings yPosColumn=\"-1\" linePlacementFlags=\"10\" placement=\"0\" dist=\"0\" xPosColumn=\"-1\" priority=\"0\" obstacle=\"0\" zIndex=\"0\" showAll=\"1\"/> \
                          <annotationform></annotationform> \
                          <excludeAttributesWMS/> \
                          <excludeAttributesWFS/> \
                          <attributeactions/> \
                          <editorlayout>generatedlayout</editorlayout> \
                          <widgets/> \
                          <conditionalstyles> \
                            <rowstyles/> \
                            <fieldstyles/> \
                          </conditionalstyles> \
                          <layerGeometryType>0</layerGeometryType> \
                        </qgis>");
    ASSERT_NE(style, nullptr);

    // Delete resource group
    EXPECT_EQ(ngsCatalogObjectDelete(group), COD_SUCCESS);

    // Delete connection
    EXPECT_EQ(ngsCatalogObjectDelete(connection), COD_SUCCESS);

    ngsUnInit();
}

TEST(NGWTests, TestCreateWebMap) {
    initLib();
    // Create web map with groups
    ngsUnInit();
}

TEST(NGWTests, TestCreateWebService) {
    initLib();

    // Create connection
    auto connection = createConnection("sandbox.nextgis.com");
    ASSERT_NE(connection, nullptr);

    // Create resource group
    time_t rawTime = std::time(nullptr);
    auto groupName = "ngstest_group_" + std::to_string(rawTime);
    auto group = createGroup(connection, groupName);
    ASSERT_NE(group, nullptr);

    uploadMIToNGW("/data/bld.tab", "новый слой 4", group);

    // Find loaded layer by name
    auto vectorLayer = ngsCatalogObjectGetByName(group, "новый слой 4", 1);
    ASSERT_NE(vectorLayer, nullptr);

    char **options = nullptr;
    // WMS
    // Add style to layer
    auto style = createStyle(vectorLayer, "новый стиль",
                             "test Mapserver style", CAT_NGW_MAPSERVER_STYLE,
                             "<map><layer><styleitem>OGR_STYLE</styleitem><class><name>default</name></class></layer></map>");
    ASSERT_NE(style, nullptr);

    options = nullptr;
    options = ngsListAddNameIntValue(options, "TYPE", CAT_NGW_WMS_SERVICE);
    options = ngsListAddNameValue(options, "DESCRIPTION", "test WMS Service");
    auto wmsService = ngsCatalogObjectCreate(group, "новый wms", options);
    ngsListFree(options);

    ASSERT_NE(wmsService, nullptr);
    EXPECT_EQ(ngsNGWServiceAddLayer(wmsService, "layer1", "layer 1", style), 1);
    EXPECT_EQ(ngsCatalogObjectSync(wmsService), 1);

    // WFS
    // Create WFS service
    options = nullptr;
    options = ngsListAddNameIntValue(options, "TYPE", CAT_NGW_WFS_SERVICE);
    options = ngsListAddNameValue(options, "DESCRIPTION", "test WFS Service");

    auto wfsService = ngsCatalogObjectCreate(group, "новый wfs", options);
    ngsListFree(options);

    ASSERT_NE(wfsService, nullptr);

    EXPECT_EQ(ngsNGWServiceAddLayer(wfsService, "layer1", "layer 1", vectorLayer), 1);
    EXPECT_EQ(ngsCatalogObjectSync(wfsService), 1);

    // Delete service
    EXPECT_EQ(ngsCatalogObjectDelete(wfsService), COD_SUCCESS);

    // Delete resource group
    EXPECT_EQ(ngsCatalogObjectDelete(group), COD_SUCCESS);

    // Delete connection
    EXPECT_EQ(ngsCatalogObjectDelete(connection), COD_SUCCESS);

    ngsUnInit();
}

TEST(NGWTests, TestCreateRaster) {
    initLib();
    // Upload raster
    // Set style native + qgis
    ngsUnInit();
}

TEST(NGWTests, TestCreateLookupTable) {
    initLib();
    // create
    // change
    // delete
    ngsUnInit();
}
