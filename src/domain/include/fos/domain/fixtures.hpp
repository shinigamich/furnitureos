// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "fos/domain/model.hpp"

/// Hand-specified reference models for tests and examples. Every number here was
/// written by hand (not produced by the parametric generator) so that tests of
/// downstream modules have an independent ground truth.
namespace fos::domain::fixtures {

/// One base cabinet, carcass 800 x 560 x 720 mm (W x D x H), 18 mm particleboard,
/// 3 mm HDF back nailed onto the rear (overall depth 563 mm).
///
/// Materials:  "mat.pb18"  Particleboard 18 mm, 2800 x 2070, grain, 60.00 / sheet
///             "mat.hdf3"  HDF 3 mm, 2800 x 2070, no grain, 15.00 / sheet
/// Edge bands: "eb.abs2"   ABS 2.0 x 22 mm, 0.80 / m
///             "eb.abs04"  ABS 0.4 x 22 mm, 0.30 / m
///
/// Parts (finished L x W x T)        edges                         cut size
///  side.left   720 x 560 x 18   L1 abs2, W1+W2 abs04          719.2 x 558
///  side.right  720 x 560 x 18   L1 abs2, W1+W2 abs04          719.2 x 558
///  bottom      764 x 560 x 18   L1 abs2                       764   x 558
///  top         764 x 560 x 18   L1 abs2                       764   x 558
///  shelf.1     764 x 540 x 18   L1 abs2                       764   x 538
///  back        796 x 716 x  3   none                          796   x 716
/// (L1 of every carcass part is its front edge.)
///
/// Totals: particleboard finished area 2.3324 m2 (5 parts), HDF 0.569936 m2;
/// banding abs2 = 3.732 m (2*0.720 + 3*0.764), abs04 = 2.240 m (4*0.560).
/// Operations: side.left carries two 5 mm x 13 mm shelf-pin holes on its inner face.
/// Hardware: 4 x generic shelf support "hw.shelf_pin_5".
Model basicCabinet();

} // namespace fos::domain::fixtures
