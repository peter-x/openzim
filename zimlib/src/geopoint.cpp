/*
 * Copyright (C) 2015 Christian Reitwiessner
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <zim/geopoint.h>
#include "log.h"

log_define("zim.geopoint")

namespace zim
{
  const double GeoPoint::microDegreesToRad = 1.7453292519943295769236907684886e-08;
  const double GeoPoint::quadraticMeanRadiusCM = 637279756.0856;

  std::ostream& operator<<(std::ostream& out, const ArticleGeoPoint& p)
  {
    char data[12];
    toLittleEndian(p.latitude, data + 0);
    toLittleEndian(p.longitude, data + 4);
    toLittleEndian(p.index, data + 8);
    out.write(data, 12);
    return out;
  }

  std::istream& operator>>(std::istream& in, ArticleGeoPoint& p)
  {
    char data[12];
    in.read(data, 12);
    if (in.fail())
    {
      log_warn("error reading geo point");
      return in;
    }

    if (in.gcount() != 12)
    {
      log_warn("error reading geo point (2)");
      in.setstate(std::ios::failbit);
      return in;
    }

    p.latitude = fromLittleEndian(reinterpret_cast<uint32_t const*>(data + 0));
    p.longitude = fromLittleEndian(reinterpret_cast<uint32_t const*>(data + 4));
    p.index = fromLittleEndian(reinterpret_cast<uint32_t const*>(data + 8));
    return in;
  }

}
