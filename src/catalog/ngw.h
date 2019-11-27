/******************************************************************************
 * Project: libngstore
 * Purpose: NextGIS store and visualization support library
 * Author:  Dmitry Baryshnikov, dmitry.baryshnikov@nextgis.com
 ******************************************************************************
 *   Copyright (c) 2019 NextGIS, <info@nextgis.com>
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
#ifndef NGSNGWCONNECTION_H
#define NGSNGWCONNECTION_H

#include "objectcontainer.h"
#include "remoteconnections.h"

#include "cpl_json.h"

namespace ngs {

constexpr const char *KEY_LOGIN = "login";
constexpr const char *KEY_PASSWORD = "password";
constexpr const char *KEY_IS_GUEST = "is_guest";

/**
 * @brief NGW namespace
 */
namespace ngw {
    std::string getPermisionsUrl(const std::string &url,
                                 const std::string &resourceId);
    std::string getResourceUrl(const std::string &url,
                               const std::string &resourceId);
    std::string getChildrenUrl(const std::string &url,
                               const std::string &resourceId);
    std::string getRouteUrl(const std::string &url);
    std::string getSchemaUrl(const std::string &url);
    std::string getCurrentUserUrl(const std::string &url);
    bool checkVersion(const std::string &version, int major, int minor, int patch);
    std::string createResource(const std::string &url, const std::string &payload,
                               char **httpOptions);
    bool deleteResource(const std::string &url, const std::string &resourceId,
        char **httpOptions);
    std::string objectTypeToNGWClsType(enum ngsCatalogObjectType type);

    // Tracks
    std::string getTrackerUrl();
    bool sendTrackPoints(const std::string &payload);
}

class NGWConnectionBase : public ConnectionBase
{
public:
    std::string connectionUrl() const;
    bool isClsSupported(const std::string &cls) const;
protected:
    mutable std::string m_url, m_user;
    std::vector<std::string> m_availableCls;
};

/**
 * @brief The NGWResouceBase class
 */
class NGWResourceBase
{
public:
    explicit NGWResourceBase(NGWConnectionBase *connection = nullptr,
                            const std::string &resourceId = "0");
    bool remove();

protected:
    std::string url() const;
protected:
    std::string m_resourceId;
    NGWConnectionBase *m_connection;
};

/**
 * @brief The NGWResouce class
 */
class NGWResource : public Object, public NGWResourceBase
{
public:
    explicit NGWResource(ObjectContainer * const parent,
                         const enum ngsCatalogObjectType type,
                         const std::string &name,
                         NGWConnectionBase *connection = nullptr,
                         const std::string &resourceId = "0");
    // Object interface
public:
    virtual bool destroy() override;
};

/**
 * @brief The NGWResourceGroup class
 */
class NGWResourceGroup : public ObjectContainer, public NGWResourceBase
{
public:
    explicit NGWResourceGroup(ObjectContainer * const parent,
                         const std::string &name,
                         NGWConnectionBase *connection = nullptr,
                         const std::string &resourceId = "0");
    virtual ObjectPtr getResource(const std::string &resourceId) const;
    virtual void addResource(const CPLJSONObject &resource);

    // Object interface
public:
    virtual bool destroy() override;

    // ObjectContainer interface
public:
    virtual bool canCreate(const enum ngsCatalogObjectType type) const override;
    virtual ObjectPtr create(const enum ngsCatalogObjectType type,
                        const std::string &name, const Options &options) override;
    virtual bool canDestroy() const override;
};

/**
 * @brief The NGWTrackersGroup class
 */
class NGWTrackersGroup : public NGWResourceGroup
{
public:
    explicit NGWTrackersGroup(ObjectContainer * const parent,
                         const std::string &name,
                         NGWConnectionBase *connection = nullptr,
                         const std::string &resourceId = "0");

    // ObjectContainer interface
public:
    virtual bool canCreate(const enum ngsCatalogObjectType type) const override;
    virtual ObjectPtr create(const enum ngsCatalogObjectType type,
                        const std::string &name, const Options &options) override;
};

/**
 * @brief The NGWConnection class
 */
class NGWConnection : public NGWResourceGroup, public NGWConnectionBase
{
public:
    explicit NGWConnection(ObjectContainer * const parent,
                         const std::string &name,
                         const std::string &path);
    virtual ~NGWConnection() override;
    virtual bool loadChildren() override;

    // Object interface
public:
    virtual bool destroy() override;
    virtual Properties properties(const std::string &domain) const override;
    virtual std::string property(const std::string &key,
                                 const std::string &defaultValue,
                                 const std::string &domain) const override;
    virtual bool setProperty(const std::string &key,
                             const std::string &value,
                             const std::string &domain) override;

    // ConnectionBase
    virtual bool open() override;
    virtual void close() override;

private:
    void fillCapabilities();
    void fillProperties() const;

private:
    mutable std::string m_searchApiUrl, m_versionApiUrl;
};

}

#endif // NGSNGWCONNECTION_H
