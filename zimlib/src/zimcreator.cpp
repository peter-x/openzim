/*
 * Copyright (C) 2009 Tommi Maekitalo
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#include <zim/writer/zimcreator.h>
#include <zim/fileheader.h>
#include <zim/cluster.h>
#include <zim/blob.h>
#include <zim/endian.h>
#include <algorithm>
#include <fstream>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <stdio.h>
#include <limits>
#include <stdexcept>
#include "config.h"
#include "arg.h"
#include "md5stream.h"
#include "tee.h"
#include "log.h"

log_define("zim.writer.creator")

#define INFO(e) \
    do { \
        log_info(e); \
        std::cout << e << std::endl; \
    } while(false)

namespace zim
{
  namespace writer
  {
    ZimCreator::ZimCreator()
      : minChunkSize(1024-64),
        nextMimeIdx(0),
#ifdef ENABLE_LZMA
        compression(zimcompLzma)
#elif ENABLE_BZIP2
        compression(zimcompBzip2)
#elif ENABLE_ZLIB
        compression(zimcompZip)
#else
        compression(zimcompNone)
#endif
    {
    }

    ZimCreator::ZimCreator(int& argc, char* argv[])
      : nextMimeIdx(0),
#ifdef ENABLE_LZMA
        compression(zimcompLzma)
#elif ENABLE_BZIP2
        compression(zimcompBzip2)
#elif ENABLE_ZLIB
        compression(zimcompZip)
#else
        compression(zimcompNone)
#endif
    {
      Arg<unsigned> minChunkSizeArg(argc, argv, "--min-chunk-size");
      if (minChunkSizeArg.isSet())
        minChunkSize = minChunkSizeArg;
      else
        minChunkSize = Arg<unsigned>(argc, argv, 's', 1024-64);

#ifdef ENABLE_ZLIB
      if (Arg<bool>(argc, argv, "--zlib"))
        compression = zimcompZip;
#endif
#ifdef ENABLE_BZIP2
      if (Arg<bool>(argc, argv, "--bzip2"))
        compression = zimcompBzip2;
#endif
#ifdef ENABLE_LZMA
      if (Arg<bool>(argc, argv, "--lzma"))
        compression = zimcompLzma;
#endif
    }

    void ZimCreator::create(const std::string& fname, ArticleSource& src)
    {
      isEmpty = true;

      std::string basename = fname;
      basename =  (fname.size() > 4 && fname.compare(fname.size() - 4, 4, ".zim") == 0)
                     ? fname.substr(0, fname.size() - 4)
                     : fname;
      log_debug("basename " << basename);

      INFO("create directory entries");
      createDirents(src);
      INFO(dirents.size() << " directory entries created");

      INFO("create title index");
      createTitleIndex(src);
      INFO(dirents.size() << " title index created");

      INFO("create clusters");
      createClusters(src, basename + ".tmp");
      INFO(clusterOffsets.size() << " clusters created");

      INFO("create geo index");
      createGeoIndex();
      INFO(articleGeoPoints.size() << " geo points indexed");

      INFO("fill header");
      fillHeader(src);

      INFO("write zimfile");
      write(basename + ".zim", basename + ".tmp");

      ::remove((basename + ".tmp").c_str());

      INFO("ready");
    }

    void ZimCreator::createDirents(ArticleSource& src)
    {
      INFO("collect articles");

      const Article* article;
      while ((article = src.getNextArticle()) != 0)
      {
        Dirent dirent;
        dirent.setAid(article->getAid());
        dirent.setUrl(article->getNamespace(), article->getUrl());
        dirent.setTitle(article->getTitle());
        dirent.setParameter(article->getParameter());

        log_debug("article " << dirent.getLongUrl() << " fetched");

        if (article->isRedirect())
        {
          dirent.setRedirect(0);
          dirent.setRedirectAid(article->getRedirectAid());
          log_debug("is redirect to " << dirent.getRedirectAid());
        }
        else if (article->isLinktarget())
        {
          dirent.setLinktarget();
        }
        else if (article->isDeleted())
        {
          dirent.setDeleted();
        }
        else
        {
          dirent.setArticle(getMimeTypeIdx(article->getMimeType()), 0, 0);
          dirent.setCompress(article->shouldCompress());
          log_debug("is article; mimetype " << dirent.getMimeType());
        }

        dirents.push_back(dirent);
      }

      // sort
      INFO("sort " << dirents.size() << " directory entries (aid)");
      std::sort(dirents.begin(), dirents.end(), compareAid);

      // remove invalid redirects
      INFO("remove invalid redirects from " << dirents.size() << " directory entries");
      DirentsType::size_type di = 0;
      while (di < dirents.size())
      {
        if (dirents[di].isRedirect())
        {
          log_debug("check " << dirents[di].getTitle() << " redirect to " << dirents[di].getRedirectAid() << " (" << di << '/' << dirents.size() << ')');

          if (!std::binary_search(dirents.begin(), dirents.end(), Dirent(dirents[di].getRedirectAid()), compareAid))
          {
            log_debug("remove invalid redirection " << dirents[di].getTitle());
            dirents.erase(dirents.begin() + di);
            continue;
          }
        }

        ++di;
      }

      // sort
      INFO("sort " << dirents.size() << " directory entries (url)");
      std::sort(dirents.begin(), dirents.end(), compareUrl);

      // set index
      INFO("set index");
      unsigned idx = 0;
      for (DirentsType::iterator di = dirents.begin(); di != dirents.end(); ++di)
        di->setIdx(idx++);

      // sort
      log_debug("sort " << dirents.size() << " directory entries (aid)");
      std::sort(dirents.begin(), dirents.end(), compareAid);

      // translate redirect aid to index
      INFO("translate redirect aid to index");
      for (DirentsType::iterator di = dirents.begin(); di != dirents.end(); ++di)
      {
        if (di->isRedirect())
        {
          DirentsType::iterator ddi = std::lower_bound(dirents.begin(), dirents.end(), di->getRedirectAid(), compareAid);
          if (ddi != dirents.end() && ddi->getAid() == di->getRedirectAid())
          {
            log_debug("redirect aid=" << ddi->getAid() << " redirect index=" << ddi->getIdx());
            di->setRedirect(ddi->getIdx());
          }
          else
          {
            std::ostringstream msg;
            msg << "internal error: redirect aid " << di->getRedirectAid() << " not found";
            log_fatal(msg.str());
            throw std::runtime_error(msg.str());
          }
        }
      }

      // sort
      log_debug("sort " << dirents.size() << " directory entries (url)");
      std::sort(dirents.begin(), dirents.end(), compareUrl);

    }

    namespace
    {
      class CompareTitle
      {
          ZimCreator::DirentsType& dirents;

        public:
          explicit CompareTitle(ZimCreator::DirentsType& dirents_)
            : dirents(dirents_)
            { }
          bool operator() (size_type titleIdx1, size_type titleIdx2) const
          {
            Dirent d1 = dirents[titleIdx1];
            Dirent d2 = dirents[titleIdx2];
            return d1.getNamespace() < d2.getNamespace()
               || (d1.getNamespace() == d2.getNamespace()
                && d1.getTitle() < d2.getTitle());
          }
      };
    }

    void ZimCreator::createTitleIndex(ArticleSource& src)
    {
      titleIdx.resize(dirents.size());
      for (DirentsType::size_type n = 0; n < dirents.size(); ++n)
        titleIdx[n] = dirents[n].getIdx();

      CompareTitle compareTitle(dirents);
      std::sort(titleIdx.begin(), titleIdx.end(), compareTitle);
    }

    void ZimCreator::createClusters(ArticleSource& src, const std::string& tmpfname)
    {
      std::ofstream out(tmpfname.c_str());

      Cluster cluster;
      cluster.setCompression(compression);

      DirentsType::size_type count = 0, progress = 0;
      for (DirentsType::iterator di = dirents.begin(); out && di != dirents.end(); ++di, ++count)
      {
        while (progress < count * 100 / dirents.size() + 1)
        {
          INFO(progress << "% ready");
          progress += 10;
        }

        if (di->isRedirect())
          continue;

        Blob blob = src.getData(di->getAid());
        addGeoPoint(blob, di->getIdx());

        if (blob.size() > 0)
          isEmpty = false;

        if (di->isCompress())
        {
          di->setCluster(clusterOffsets.size(), cluster.count());
          cluster.addBlob(blob);
          if (cluster.size() >= minChunkSize * 1024)
          {
            log_info("compress cluster with " << cluster.count() << " articles, " << cluster.size() << " bytes; current title \"" << di->getTitle() << '\"');

            clusterOffsets.push_back(out.tellp());
            out << cluster;
            log_debug("cluster compressed");
            cluster.clear();
            cluster.setCompression(compression);
          }
        }
        else
        {
          if (cluster.count() > 0)
          {
            clusterOffsets.push_back(out.tellp());
            cluster.setCompression(compression);
            out << cluster;
            cluster.clear();
            cluster.setCompression(compression);
          }

          di->setCluster(clusterOffsets.size(), cluster.count());
          clusterOffsets.push_back(out.tellp());
          Cluster c;
          c.addBlob(blob);
          c.setCompression(zimcompNone);
          out << c;
        }
      }

      if (cluster.count() > 0)
      {
        clusterOffsets.push_back(out.tellp());
        cluster.setCompression(compression);
        out << cluster;
      }

      if (!out)
        throw std::runtime_error("failed to write temporary cluster file");

      clustersSize = out.tellp();
    }

    void ZimCreator::addGeoPoint(const Blob& blob, size_t index)
    {
      static const char* metaTag = "<meta name=\"geo.position\" content=\"";
      static const size_t metaTagLen = std::strlen(metaTag);
      char const* tag = std::search(blob.data(), blob.end(), metaTag, metaTag + metaTagLen);
      if (tag == blob.end())
        return;
      tag += metaTagLen;

      int32_t latitudeMicroDegrees = parseCoordinateMicroDegrees(tag);
      if (!tag || *tag != ';')
        return;
      tag++;
      int32_t longitudeMicroDegrees = parseCoordinateMicroDegrees(tag);
      if (!tag)
        return;

      ArticleGeoPoint p;
      p.index = index;
      p.latitude = Latitude::fromMicroDegrees(latitudeMicroDegrees);
      p.longitude = Longitude::fromMicroDegrees(longitudeMicroDegrees);
      articleGeoPoints.push_back(p);
    }

    void ZimCreator::createGeoIndex()
    {
      // Header:
      // <index_count> <start_1> <start_2> ... <end_n>
      // Here: only one index
      char indexHeader[3 * 4];
      toLittleEndian(uint32_t(1), indexHeader + 0);
      toLittleEndian(uint32_t(sizeof(indexHeader)), indexHeader + 4);
      geoIndex.write(indexHeader, sizeof(indexHeader));

      createGeoIndexPart(articleGeoPoints.begin(), articleGeoPoints.end(), 0);

      uint32_t size = geoIndex.tellp();
      toLittleEndian(size, indexHeader + 8);
      geoIndex.seekp(0);
      geoIndex.write(indexHeader, sizeof(indexHeader));
    }

    void ZimCreator::createGeoIndexPart(ArticleGeoPointIterator begin, ArticleGeoPointIterator end, unsigned depth)
    {
      char data[4];
      // If we have less than 10 points or all remaining points are equal
      if (end < begin + 10 || end == std::adjacent_find(begin, end, std::not_equal_to<ArticleGeoPoint>()))
      {
        toLittleEndian(uint32_t(0), data);
        if (end <= begin)
          toLittleEndian(uint32_t(0), data);
        else
          toLittleEndian(uint32_t(end - begin), data);
        geoIndex.write(data, 4);
        for (; begin < end; ++begin)
          geoIndex << *begin;
      }
      else
      {
        if (depth % 2)
          std::sort(begin, end, AxisComparator<1>());
        else
          std::sort(begin, end, AxisComparator<0>());
        ArticleGeoPointIterator median = begin + (end - begin) / 2;
        uint32_t medianValue = median->axisValue(depth % 2);
        if (medianValue == 0)
        {
          // We cannot have such a median value, because this would make this node a leaf node.
          log_warn("Dropping points from geo index: Median value zero encountered - too many small coordinates.");
          createGeoIndexPart(begin + 1, end, depth);
          return;
        }
        if (median->axisValue(depth % 2) == begin->axisValue(depth % 2))
        {
          // median is equal to the first value, increment until it is not equal anymore
          while (median < end && median->axisValue(depth % 2) == begin->axisValue(depth % 2))
            ++median;
          if (median < end)
            medianValue = median->axisValue(depth % 2);
        }
        else
        {
          // Decrement the median as long as the value is the same.
          while (median > begin && (median - 1)->axisValue(depth % 2) == medianValue)
            --median;
        }
        toLittleEndian(medianValue, data);
        geoIndex.write(data, 4);
        std::ostream::pos_type offsetPos = geoIndex.tellp();
        // will be overwritten later
        geoIndex.write(data, 4);

        createGeoIndexPart(begin, median, depth + 1);

        uint32_t greaterPos = uint32_t(geoIndex.tellp());
        geoIndex.seekp(offsetPos);
        toLittleEndian(greaterPos, data);
        geoIndex.write(data, 4);
        geoIndex.seekp(0, std::ios_base::end);

        createGeoIndexPart(median, end, depth + 1);
      }
    }

    int32_t ZimCreator::parseCoordinateMicroDegrees(const char*& text)
    {
      bool negative = false;
      int32_t value = 0;
      if (*text == '-')
      {
        negative = true;
        ++text;
      }
      unsigned beyondDecimal = 0;
      for (; *text == '.' || ('0' <= *text && *text <= '9'); ++text)
      {
        if (*text == '.')
        {
          if (beyondDecimal > 0)
            break;
          else
            beyondDecimal = 1;
        }
        else
        {
          value = value * 10 + (*text - '0');
          if (beyondDecimal > 0)
            ++beyondDecimal;
        }
      }
      if (beyondDecimal == 0)
        beyondDecimal = 1;
      for (; beyondDecimal < 7; ++beyondDecimal)
        value *= 10;
      return negative ? -value : value;
    }

    void ZimCreator::fillHeader(ArticleSource& src)
    {
      std::string mainAid = src.getMainPage();
      std::string layoutAid = src.getLayoutPage();

      log_debug("main aid=" << mainAid << " layout aid=" << layoutAid);

      header.setMainPage(std::numeric_limits<size_type>::max());
      header.setLayoutPage(std::numeric_limits<size_type>::max());

      if (!mainAid.empty() || !layoutAid.empty())
      {
        for (DirentsType::const_iterator di = dirents.begin(); di != dirents.end(); ++di)
        {
          if (mainAid == di->getAid())
          {
            log_debug("main idx=" << di->getIdx());
            header.setMainPage(di->getIdx());
          }

          if (layoutAid == di->getAid())
          {
            log_debug("layout idx=" << di->getIdx());
            header.setLayoutPage(di->getIdx());
          }
        }
      }

      header.setUuid( src.getUuid() );
      header.setArticleCount( dirents.size() );
      header.setUrlPtrPos( urlPtrPos() );
      header.setMimeListPos( mimeListPos() );
      header.setTitleIdxPos( titleIdxPos() );
      header.setClusterCount( clusterOffsets.size() );
      header.setClusterPtrPos( clusterPtrPos() );
      header.setChecksumPos( checksumPos() );
      header.setGeoIdxPos( geoIdxPos() );

      log_debug(
            "mimeListSize=" << mimeListSize() <<
           " mimeListPos=" << mimeListPos() <<
           " indexPtrSize=" << urlPtrSize() <<
           " indexPtrPos=" << urlPtrPos() <<
           " indexSize=" << indexSize() <<
           " indexPos=" << indexPos() <<
           " geoIndexSize=" << geoIdxSize() <<
           " geoIndexPos=" << geoIdxPos() <<
           " clusterPtrSize=" << clusterPtrSize() <<
           " clusterPtrPos=" << clusterPtrPos() <<
           " clusterCount=" << clusterCount() <<
           " articleCount=" << articleCount() <<
           " articleCount=" << dirents.size() <<
           " urlPtrPos=" << header.getUrlPtrPos() <<
           " titleIdxPos=" << header.getTitleIdxPos() <<
           " clusterCount=" << header.getClusterCount() <<
           " clusterPtrPos=" << header.getClusterPtrPos() <<
           " checksumPos=" << checksumPos()
           );
    }

    void ZimCreator::write(const std::string& fname, const std::string& tmpfname)
    {
      std::ofstream zimfile(fname.c_str());
      Md5stream md5;
      Tee out(zimfile, md5);

      out << header;

      log_debug("after writing header - pos=" << zimfile.tellp());

      // write mime type list
      std::vector<std::string> oldMImeList;
      std::vector<std::string> newMImeList;
      std::vector<uint16_t> mapping;

      for (RMimeTypes::const_iterator it = rmimeTypes.begin(); it != rmimeTypes.end(); ++it)
      {
        oldMImeList.push_back(it->second);
        newMImeList.push_back(it->second);
      }

      mapping.resize(oldMImeList.size());
      std::sort(newMImeList.begin(), newMImeList.end());

      for (unsigned i=0; i<oldMImeList.size(); ++i)
      {
        for (unsigned j=0; j<newMImeList.size(); ++j)
        {
          if (oldMImeList[i] == newMImeList[j])
            mapping[i] = static_cast<uint16_t>(j);
        }
      }

      for (unsigned i=0; i<dirents.size(); ++i)
      {
        if (dirents[i].isArticle())
          dirents[i].setMimeType(mapping[dirents[i].getMimeType()]);
      }

      for (unsigned i=0; i<newMImeList.size(); ++i)
      {
        out << newMImeList[i] << '\0';
      }

      out << '\0';

      // write url ptr list

      offset_type off = indexPos();
      for (DirentsType::const_iterator it = dirents.begin(); it != dirents.end(); ++it)
      {
        offset_type ptr0 = fromLittleEndian<offset_type>(&off);
        out.write(reinterpret_cast<const char*>(&ptr0), sizeof(ptr0));
        off += it->getDirentSize();
      }

      log_debug("after writing direntPtr - pos=" << out.tellp());

      // write title index

      for (SizeVectorType::const_iterator it = titleIdx.begin(); it != titleIdx.end(); ++it)
      {
        size_type v = fromLittleEndian<size_type>(&*it);
        out.write(reinterpret_cast<const char*>(&v), sizeof(v));
      }

      log_debug("after writing fileIdxList - pos=" << out.tellp());

      // write geo index

      std::string geoIndexBytes = geoIndex.str();
      out.write(geoIndexBytes.c_str(), geoIndexBytes.length());

      log_debug("after writing geoIdx - pos=" << out.tellp());

      // write directory entries

      for (DirentsType::const_iterator it = dirents.begin(); it != dirents.end(); ++it)
      {
        out << *it;
        log_debug("write " << it->getTitle() << " dirent.size()=" << it->getDirentSize() << " pos=" << out.tellp());
      }

      log_debug("after writing dirents - pos=" << out.tellp());

      // write cluster offset list

      off += clusterOffsets.size() * sizeof(offset_type);
      for (OffsetsType::const_iterator it = clusterOffsets.begin(); it != clusterOffsets.end(); ++it)
      {
        offset_type o = (off + *it);
        offset_type ptr0 = fromLittleEndian<offset_type>(&o);
        out.write(reinterpret_cast<const char*>(&ptr0), sizeof(ptr0));
      }

      log_debug("after writing clusterOffsets - pos=" << out.tellp());

      // write cluster data

      if (!isEmpty)
      {
        std::ifstream blobsfile(tmpfname.c_str());
        out << blobsfile.rdbuf();
      }
      else
        log_warn("no data found");

      if (!out)
        throw std::runtime_error("failed to write zimfile");

      log_debug("after writing clusterData - pos=" << out.tellp());
      unsigned char digest[16];
      md5.getDigest(digest);
      zimfile.write(reinterpret_cast<const char*>(digest), 16);
    }

    offset_type ZimCreator::mimeListSize() const
    {
      offset_type ret = 1;
      for (RMimeTypes::const_iterator it = rmimeTypes.begin(); it != rmimeTypes.end(); ++it)
        ret += (it->second.size() + 1);
      return ret;
    }

    offset_type ZimCreator::indexSize() const
    {
      offset_type s = 0;

      for (DirentsType::const_iterator it = dirents.begin(); it != dirents.end(); ++it)
        s += it->getDirentSize();

      return s;
    }

    uint16_t ZimCreator::getMimeTypeIdx(const std::string& mimeType)
    {
      MimeTypes::const_iterator it = mimeTypes.find(mimeType);
      if (it == mimeTypes.end())
      {
        if (nextMimeIdx >= std::numeric_limits<uint16_t>::max())
          throw std::runtime_error("too many distinct mime types");
        mimeTypes[mimeType] = nextMimeIdx;
        rmimeTypes[nextMimeIdx] = mimeType;
        return nextMimeIdx++;
      }

      return it->second;
    }

    const std::string& ZimCreator::getMimeType(uint16_t mimeTypeIdx) const
    {
      RMimeTypes::const_iterator it = rmimeTypes.find(mimeTypeIdx);
      if (it == rmimeTypes.end())
        throw std::runtime_error("mime type index not found");
      return it->second;
    }

  }
}
