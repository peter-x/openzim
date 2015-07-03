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

#ifndef ZIM_GEOPOINT_H
#define ZIM_GEOPOINT_H

#include <string>
#include <istream>
#include <ostream>
#include <limits>
#include <zim/zim.h>
#include <zim/endian.h>

namespace zim
{
  /// Utility to provide conversion functions from and to the internal coordinat representation.
  template <bool IsLatitude>
  struct GeoAxis
  {
      static int32_t toMicroDegrees(uint32_t value)
      {
        int32_t v = int32_t((uint64_t(value) * 360000000) >> 32) - 180000000;
        return IsLatitude ? v / 2 : v;
      }

      static uint32_t fromMicroDegrees(int32_t coordMicroDegrees)
      {
        // input range: -180 000 000 to +180 000 000
        // output range: 0 to 4 294 967 295
        if (IsLatitude)
          coordMicroDegrees *= 2;
        return uint32_t((uint64_t(coordMicroDegrees + 180000000) << 32) / 360000000);
      }
  };
  typedef GeoAxis<true> Latitude;
  typedef GeoAxis<false> Longitude;

  struct GeoPoint
  {
      uint32_t latitude;
      uint32_t longitude;

      uint32_t axisValue(unsigned axis) const
      {
        return axis == 0 ? latitude : longitude;
      }
      uint32_t& axisValue(unsigned axis)
      {
        return axis == 0 ? latitude : longitude;
      }
      bool valid() const { return latitude != 0 || longitude != 0; }
      bool operator<=(GeoPoint const& _other) const
      {
        return latitude <= _other.latitude && longitude <= _other.longitude;
      }
      bool operator!=(GeoPoint const& _other) const
      {
        return latitude != _other.latitude || longitude != _other.longitude;
      }
  };

  struct ArticleGeoPoint : GeoPoint
  {
      size_type index;

      ArticleGeoPoint(): index(std::numeric_limits<size_type>::max()) {}

      friend std::ostream& operator<<(std::ostream& out, ArticleGeoPoint const& p);
      friend std::istream& operator>>(std::istream& in, ArticleGeoPoint& p);
  };

  template <unsigned Axis>
  struct AxisComparator
  {
      bool operator()(GeoPoint const& a, GeoPoint const& b) const
      {
        return a.axisValue(Axis) < b.axisValue(Axis);
      }
  };

}

#endif // ZIM_GEOPOINT_H
