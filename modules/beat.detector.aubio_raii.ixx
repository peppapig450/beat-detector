module;
#include <aubio/types.h>

#include <aubio/fvec.h>
#include <aubio/onset/onset.h>
#include <aubio/pitch/pitch.h>
#include <aubio/tempo/tempo.h>

#include <memory>

export module beat.detector:aubio_raii;

namespace aubio_raii {

struct TempoDeleter {
  void operator()(aubio_tempo_t* tempo) const noexcept {
    if (tempo != nullptr) {
      del_aubio_tempo(tempo);
    }
  }
};

struct FVecDeleter {
  void operator()(fvec_t* vec) const noexcept {
    if (vec != nullptr) {
      del_fvec(vec);
    }
  }
};

struct OnsetDeleter {
  void operator()(aubio_onset_t* onset) const noexcept {
    if (onset != nullptr) {
      del_aubio_onset(onset);
    }
  }
};

struct PitchDeleter {
  void operator()(aubio_pitch_t* pitch) const noexcept {
    if (pitch != nullptr) {
      del_aubio_pitch(pitch);
    }
  }
};

using TempoPtr = std::unique_ptr<aubio_tempo_t, TempoDeleter>;
using FVecPtr  = std::unique_ptr<fvec_t, FVecDeleter>;
using OnsetPtr = std::unique_ptr<aubio_onset_t, OnsetDeleter>;
using PitchPtr = std::unique_ptr<aubio_pitch_t, PitchDeleter>;

}  // namespace aubio_raii
