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
#include <cmath>
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
      static const double microDegreesToRad;
      static const double quadraticMeanRadiusCM;
      uint32_t latitude;
      uint32_t longitude;

      GeoPoint() {}
      GeoPoint(uint32_t _latitude, uint32_t _longitude): latitude(_latitude), longitude(_longitude) {}
      uint32_t axisValue(unsigned axis) const
      {
        return axis == 0 ? latitude : longitude;
      }
      uint32_t& axisValue(unsigned axis)
      {
        return axis == 0 ? latitude : longitude;
      }
      /// @returns the geodetic distance between this point and @a _other in centimeters.
      uint32_t distance(const GeoPoint& _other) const
      {
        double latArc = microDegreesToRad * (Latitude::toMicroDegrees(latitude) - Latitude::toMicroDegrees(_other.latitude));
        double longArc = microDegreesToRad * (Longitude::toMicroDegrees(longitude) - Longitude::toMicroDegrees(_other.longitude));
        double latH = std::sin(latArc * .5);
        latH *= latH;
        double longH = std::sin(longArc * .5);
        longH *= longH;
        double tmp = std::cos(microDegreesToRad * Latitude::toMicroDegrees(latitude)) *
                     std::cos(microDegreesToRad * Latitude::toMicroDegrees(_other.latitude));
        return uint32_t(quadraticMeanRadiusCM * 2.0 * std::asin(std::sqrt(latH + tmp * longH)));
      }
      bool valid() const { return latitude != 0 || longitude != 0; }
      bool operator<(GeoPoint const& _other) const
      {
        return latitude < _other.latitude || longitude < _other.longitude;
      }
      bool operator<=(GeoPoint const& _other) const
      {
        return latitude <= _other.latitude && longitude <= _other.longitude;
      }
      bool operator!=(GeoPoint const& _other) const
      {
        return latitude != _other.latitude || longitude != _other.longitude;
      }
      GeoPoint operator-(GeoPoint const& _diff) const
      {
        GeoPoint r;
        r.latitude = _diff.latitude < latitude ? latitude - _diff.latitude : 0;
        r.longitude = _diff.longitude < longitude ? longitude - _diff.longitude : 0;
        return r;
      }
      GeoPoint operator+(GeoPoint const& _diff) const
      {
        GeoPoint r;
        const uint32_t max = std::numeric_limits<uint32_t>::max();
        r.latitude = uint64_t(_diff.latitude) + latitude <= max ? latitude + _diff.latitude : max;
        r.longitude = uint64_t(_diff.longitude) + longitude <= max ? longitude + _diff.longitude : max;
        return r;
      }
      /// @returns a pseudo-rectangle approximately containing a circle of distanceCM cm around this point.
      std::pair<GeoPoint, GeoPoint> enclosingPseudoRectangle(uint32_t radiusCM) const
      {
        GeoPoint diff;
        diff.latitude = Latitude::fromMicroDegrees(std::asin(radiusCM / quadraticMeanRadiusCM) / microDegreesToRad);
        double longRadiusCM = std::cos(Latitude::toMicroDegrees(latitude) * microDegreesToRad) * quadraticMeanRadiusCM;
        diff.longitude = Longitude::fromMicroDegrees(std::asin(radiusCM / longRadiusCM) / microDegreesToRad);
        return std::make_pair(*this - diff, *this + diff);
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
