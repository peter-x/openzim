/*
 * Copyright (C) 2006 Tommi Maekitalo
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

#ifndef ZIM_ARTICLE_H
#define ZIM_ARTICLE_H

#include <string>
#include <zim/zim.h>
#include <zim/dirent.h>
#include <zim/qunicode.h>
#include <zim/file.h>
#include <limits>

namespace zim
{
  class Article
  {
    private:
      File file;
      size_type idx;

    public:
      Article()
        : idx(std::numeric_limits<size_type>::max())
          { }

      Article(const File& file_, size_type idx_)
        : file(file_),
          idx(idx_)
          { }

      Dirent getDirent() const                { return const_cast<File&>(file).getDirent(idx); }

      const std::string&
                  getParameter() const        { return getDirent().getParameter(); }

      QUnicodeString getTitle() const         { return getDirent().getTitle(); }

      MimeType    getLibraryMimeType() const  { return getDirent().getMimeType(); }
      const std::string&
                  getMimeType() const;

      bool        isRedirect() const          { return getDirent().isRedirect(); }

      char        getNamespace() const        { return getDirent().getNamespace(); }

      size_type   getRedirectIndex() const    { return getDirent().getRedirectIndex(); }
      Article     getRedirectArticle() const  { return Article(file, getRedirectIndex()); }

      size_type   getArticleSize() const;

      bool operator< (const Article& a) const
        { return getNamespace() < a.getNamespace()
              || getNamespace() == a.getNamespace()
               && getTitle() < a.getTitle(); }

      Cluster getCluster() const
        { return file.getCluster(getDirent().getClusterNumber()); }

      Blob getData() const
      {
        Dirent dirent = getDirent();
        return const_cast<File&>(file).getBlob(dirent.getClusterNumber(), dirent.getBlobNumber());
      }

      const File& getFile() const    { return file; }
      size_type   getIndex() const   { return idx; }

      QUnicodeString getUrl() const;
  };

}

#endif // ZIM_ARTICLE_H

