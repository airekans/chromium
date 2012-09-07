# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# The test takes a list of URLs through stdin and prints results in CSV format.
# Example: python run_scroll_test.py < data/urls.txt > test_results.csv
import chrome_remote_control

import multi_page_benchmark
import util


class FirstPaintTimeBenchmark(multi_page_benchmark.MultiPageBenchmark):
  def MeasurePage(self, page, tab):
    tab.runtime.Execute("""
        window.__rafFired = false;
        window.webkitRequestAnimationFrame(function() {
            window.__rafFired  = true;
        });
    """)
    chrome_remote_control.WaitFor(
        lambda: tab.runtime.Evaluate('window.__rafFired'), 60)

    first_paint_secs = tab.runtime.Evaluate(
      'chrome.loadTimes().firstPaintTime - chrome.loadTimes().startLoadTime')

    return {
        "first_paint_secs": util.RoundTo3DecimalPlaces(first_paint_secs)
        }

def Main():
  return multi_page_benchmark.Main(FirstPaintTimeBenchmark())
