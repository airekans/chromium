// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_THEMES_BROWSER_THEME_PACK_H_
#define CHROME_BROWSER_THEMES_BROWSER_THEME_PACK_H_

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "base/sequenced_task_runner_helpers.h"
#include "chrome/common/extensions/extension.h"
#include "content/public/browser/browser_thread.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/layout.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image.h"

namespace base {
class DictionaryValue;
class FilePath;
class RefCountedMemory;
}

namespace extensions {
class Extensions;
}

namespace ui {
class DataPack;
}

// An optimized representation of a theme, backed by a mmapped DataPack.
//
// The idea is to pre-process all images (tinting, compositing, etc) at theme
// install time, save all the PNG-ified data into an mmappable file so we don't
// suffer multiple file system access times, therefore solving two of the
// problems with the previous implementation.
//
// A note on const-ness. All public, non-static methods are const.  We do this
// because once we've constructed a BrowserThemePack through the
// BuildFromExtension() interface, we WriteToDisk() on a thread other than the
// UI thread that consumes a BrowserThemePack. There is no locking; thread
// safety between the writing thread and the UI thread is ensured by having the
// data be immutable.
//
// BrowserThemePacks are always deleted on the file thread because in the
// common case, they are backed by mmapped data and the unmmapping operation
// will trip our IO on the UI thread detector.
class BrowserThemePack : public base::RefCountedThreadSafe<
    BrowserThemePack, content::BrowserThread::DeleteOnFileThread> {
 public:
  // Builds the theme pack from all data from |extension|. This is often done
  // on a separate thread as it takes so long. This can fail and return NULL in
  // the case where the theme has invalid data.
  static scoped_refptr<BrowserThemePack> BuildFromExtension(
      const extensions::Extension* extension);

  // Builds the theme pack from a previously performed WriteToDisk(). This
  // operation should be relatively fast, as it should be an mmap() and some
  // pointer swizzling. Returns NULL on any error attempting to read |path|.
  static scoped_refptr<BrowserThemePack> BuildFromDataPack(
      const base::FilePath& path, const std::string& expected_id);

  // Builds a data pack on disk at |path| for future quick loading by
  // BuildFromDataPack(). Often (but not always) called from the file thread;
  // implementation should be threadsafe because neither thread will write to
  // |image_memory_| and the worker thread will keep a reference to prevent
  // destruction.
  bool WriteToDisk(const base::FilePath& path) const;

  // If this theme specifies data for the corresponding |id|, return true and
  // write the corresponding value to the output parameter. These functions
  // don't return the default data. These methods should only be called from
  // the UI thread. (But this isn't enforced because of unit tests).
  bool GetTint(int id, color_utils::HSL* hsl) const;
  bool GetColor(int id, SkColor* color) const;
  bool GetDisplayProperty(int id, int* result) const;

  // Returns the theme pack image for |id|. Returns an empty image if an image
  // is not found.
  gfx::Image GetImageNamed(int id);

  // Returns the raw PNG encoded data for IDR_THEME_NTP_*. This method is only
  // supposed to work for the NTP attribution and background resources.
  base::RefCountedMemory* GetRawData(int id,
                                     ui::ScaleFactor scale_factor) const;

  // Returns the set of image idrs which can be overwritten by a user provided
  // theme.
  static void GetThemeableImageIDRs(std::set<int>* result);

  // Whether this theme provides an image for |id|.
  bool HasCustomImage(int id) const;

 private:
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::FILE>;
  friend class base::DeleteHelper<BrowserThemePack>;
  friend class BrowserThemePackTest;

  // Cached images.
  typedef std::map<int, gfx::Image> ImageCache;

  // The raw PNG memory associated with a certain id.
  typedef std::map<int, scoped_refptr<base::RefCountedMemory> > RawImages;

  // The type passed to ui::DataPack::WritePack.
  typedef std::map<uint16, base::StringPiece> RawDataForWriting;

  // Maps scale factors (enum values) to file paths.
  typedef std::map<ui::ScaleFactor, base::FilePath> ScaleFactorToFileMap;

  // Maps image ids to maps of scale factors to file paths.
  typedef std::map<int, ScaleFactorToFileMap> FilePathMap;

  // Default. Everything is empty.
  BrowserThemePack();

  virtual ~BrowserThemePack();

  // Builds a header ready to write to disk.
  void BuildHeader(const extensions::Extension* extension);

  // Transforms the JSON tint values into their final versions in the |tints_|
  // array.
  void BuildTintsFromJSON(const base::DictionaryValue* tints_value);

  // Transforms the JSON color values into their final versions in the
  // |colors_| array and also fills in unspecified colors based on tint values.
  void BuildColorsFromJSON(const base::DictionaryValue* color_value);

  // Implementation details of BuildColorsFromJSON().
  void ReadColorsFromJSON(const base::DictionaryValue* colors_value,
                          std::map<int, SkColor>* temp_colors);
  void GenerateMissingColors(std::map<int, SkColor>* temp_colors);

  // Transforms the JSON display properties into |display_properties_|.
  void BuildDisplayPropertiesFromJSON(
      const base::DictionaryValue* display_value);

  // Parses the image names out of an extension.
  void ParseImageNamesFromJSON(const base::DictionaryValue* images_value,
                               const base::FilePath& images_path,
                               FilePathMap* file_paths) const;

  // Helper function to populate the FilePathMap.
  void AddFileAtScaleToMap(const std::string& image_name,
                           ui::ScaleFactor scale_factor,
                           const base::FilePath& image_path,
                           FilePathMap* file_paths) const;

  // Creates the data for |source_images_| from |file_paths|.
  void BuildSourceImagesArray(const FilePathMap& file_paths);

  // Loads the unmodified images packed in the extension to SkBitmaps. Returns
  // true if all images loaded.
  bool LoadRawBitmapsTo(const FilePathMap& file_paths,
                        ImageCache* image_cache);

  // Populate |images| cache with empty gfx::Images. Image reps are lazily
  // generated when an image rep is requested via ImageSkia::GetRepresentation.
  // Source and destination is |images|.
  void CreateImages(ImageCache* images) const;

  // Crops images down to a size such that most of the cropped image will be
  // displayed in the UI. Cropping is useful because images from custom themes
  // can be of any size. Source and destination is |images|.
  void CropImages(ImageCache* images) const;

  // Creates tinted and composited frame images. Source and destination is
  // |images|.
  void CreateFrameImages(ImageCache* images) const;

  // Creates button images tinted with |button_tint| and places them in
  // processed_images.
  void CreateTintedButtons(const color_utils::HSL& button_tint,
                           ImageCache* processed_images) const;

  // Creates the semi-transparent tab background images, putting the results
  // in |images|. Must be called after GenerateFrameImages().
  void CreateTabBackgroundImages(ImageCache* images) const;

  // Takes all the SkBitmaps in |images|, encodes them as PNGs and places
  // them in |reencoded_images|.
  void RepackImages(const ImageCache& images,
                    RawImages* reencoded_images) const;

  // Takes all images in |source| and puts them in |destination|, freeing any
  // image already in |destination| that |source| would overwrite.
  void MergeImageCaches(const ImageCache& source,
                        ImageCache* destination) const;

  // Copies images from |source| to |destination| such that the lifetimes of
  // the images in |destination| are not affected by the lifetimes of the
  // images in |source|.
  void CopyImagesTo(const ImageCache& source, ImageCache* destination) const;

  // Changes the RefCountedMemory based |images| into StringPiece data in |out|.
  void AddRawImagesTo(const RawImages& images, RawDataForWriting* out) const;

  // Retrieves the tint OR the default tint. Unlike the public interface, we
  // always need to return a reasonable tint here, instead of partially
  // querying if the tint exists.
  color_utils::HSL GetTintInternal(int id) const;

  // Returns a unique id to use to store the raw bitmap for |prs_id| at
  // |scale_factor| in memory.
  int GetRawIDByPersistentID(int prs_id, ui::ScaleFactor scale_factor) const;

  // Returns true if the |key| specifies a valid scale (e.g. "100") and
  // the corresponding scale factor is currently in use. If true, returns
  // the scale factor in |scale_factor|.
  bool GetScaleFactorFromManifestKey(const std::string& key,
                                     ui::ScaleFactor* scale_factor) const;

  // Data pack, if we have one.
  scoped_ptr<ui::DataPack> data_pack_;

  // All structs written to disk need to be packed; no alignment tricks here,
  // please.
#pragma pack(push,1)
  // Header that is written to disk.
  struct BrowserThemePackHeader {
    // Numeric version to make sure we're compatible in the future.
    int32 version;

    // 1 if little_endian. 0 if big_endian. On mismatch, abort load.
    int32 little_endian;

    // theme_id without NULL terminator.
    uint8 theme_id[16];
  } *header_;

  // The remaining structs represent individual entries in an array. For the
  // following three structs, BrowserThemePack will either allocate an array or
  // will point directly to mmapped data.
  struct TintEntry {
    int32 id;
    double h;
    double s;
    double l;
  } *tints_;

  struct ColorPair {
    int32 id;
    SkColor color;
  } *colors_;

  struct DisplayPropertyPair {
    int32 id;
    int32 property;
  } *display_properties_;

  // A list of included source images. A pointer to a -1 terminated array of
  // our persistent IDs.
  int* source_images_;
#pragma pack(pop)

  // The scale factors represented by the images in the theme pack.
  std::vector<ui::ScaleFactor> scale_factors_;

  // References to raw PNG data. This map isn't touched when |data_pack_| is
  // non-NULL; |image_memory_| is only filled during BuildFromExtension(). Any
  // image data that needs to be written to the DataPack during WriteToDisk()
  // needs to be in |image_memory_|.
  RawImages image_memory_;

  // Loaded images. These are loaded from |image_memory_|, from |data_pack_|,
  // and by BuildFromExtension(). These images should only be accessed on the UI
  // thread.
  ImageCache images_on_ui_thread_;

  // Cache of images created in BuildFromExtension(). Once the theme pack is
  // created, this cache should only be accessed on the file thread. There
  // should be no IDs in |image_memory_| that are in |images_on_file_thread_|
  // or vice versa.
  ImageCache images_on_file_thread_;

  DISALLOW_COPY_AND_ASSIGN(BrowserThemePack);
};

#endif  // CHROME_BROWSER_THEMES_BROWSER_THEME_PACK_H_
