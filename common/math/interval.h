// ======================================================================== //
// Copyright 2009-2018 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "vec2.h"
#include "vec3.h"
#include "bbox.h"

namespace embree
{
  template<typename V>
    struct Interval
    {
      V lower, upper;
      
      __forceinline Interval() {}
      __forceinline Interval           ( const Interval& other ) { lower = other.lower; upper = other.upper; }
      __forceinline Interval& operator=( const Interval& other ) { lower = other.lower; upper = other.upper; return *this; }

      __forceinline Interval(const V& a) : lower(a), upper(a) {}
      __forceinline Interval(const V& lower, const V& upper) : lower(lower), upper(upper) {}
      __forceinline Interval(const BBox<V>& a) : lower(a.lower), upper(a.upper) {}
          
      /*! tests if box is empty */
      __forceinline bool empty() const { return lower > upper; }
      
      /*! computes the size of the interval */
      __forceinline V size() const { return upper - lower; }
      
      __forceinline V center() const { return 0.5f*(lower+upper); }
      
      __forceinline const Interval& extend(const Interval& other) { lower = min(lower,other.lower); upper = max(upper,other.upper); return *this; }
      __forceinline const Interval& extend(const V   & other) { lower = min(lower,other      ); upper = max(upper,other      ); return *this; }
      
      __forceinline friend Interval operator +( const Interval& a, const Interval& b ) {
        return Interval(a.lower+b.lower,a.upper+b.upper);
      }
      
      __forceinline friend Interval operator -( const Interval& a, const Interval& b ) {
        return Interval(a.lower-b.upper,a.upper-b.lower);
      }
      
      __forceinline friend Interval operator -( const Interval& a, const V& b ) {
        return Interval(a.lower-b,a.upper-b);
      }
      
      __forceinline friend Interval operator *( const Interval& a, const Interval& b )
      {
        const V ll = a.lower*b.lower;
        const V lu = a.lower*b.upper;
        const V ul = a.upper*b.lower;
        const V uu = a.upper*b.upper;
        return Interval(min(ll,lu,ul,uu),max(ll,lu,ul,uu));
      }
      
      __forceinline friend Interval merge( const Interval& a, const Interval& b) {
        return Interval(min(a.lower,b.lower),max(a.upper,b.upper));
      }
      
      __forceinline friend Interval merge( const Interval& a, const Interval& b, const Interval& c) {
        return merge(merge(a,b),c);
      }
      
      __forceinline friend Interval merge( const Interval& a, const Interval& b, const Interval& c, const Interval& d) {
        return merge(merge(a,b),merge(c,d));
      }
      
      /*! intersect bounding boxes */
      __forceinline friend const Interval intersect( const Interval& a, const Interval& b ) { return Interval(max(a.lower, b.lower), min(a.upper, b.upper)); }
      __forceinline friend const Interval intersect( const Interval& a, const Interval& b, const Interval& c ) { return intersect(a,intersect(b,c)); }
      __forceinline friend const Interval intersect( const Interval& a, const Interval& b, const Interval& c, const Interval& d ) { return intersect(intersect(a,b),intersect(c,d)); }       
      
      friend std::ostream& operator<<(std::ostream& cout, const Interval& a) {
        return cout << "[" << a.lower << ", " << a.upper << "]";
      }
      
      ////////////////////////////////////////////////////////////////////////////////
      /// Constants
      ////////////////////////////////////////////////////////////////////////////////
      
      __forceinline Interval( EmptyTy ) : lower(pos_inf), upper(neg_inf) {}
      __forceinline Interval( FullTy  ) : lower(neg_inf), upper(pos_inf) {}
    };
  
  template<> __forceinline bool Interval<float>::empty() const {
    return lower > upper;
  }
  
  /*! subset relation */
  template<typename T> __forceinline bool subset( const Interval<T>& a, const Interval<T>& b )
  { 
    if ( a.lower <= b.lower ) return false;
    if ( a.upper >= b.upper ) return false;
    return true; 
  }
  
  typedef Interval<float> Interval1f;
  typedef Vec2<Interval<float>> Interval2f;
  typedef Vec3<Interval<float>> Interval3f;
}