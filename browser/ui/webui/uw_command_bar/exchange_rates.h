// Copyright 2026 The uw Authors. All rights reserved.

#ifndef UW_BROWSER_UI_WEBUI_UW_COMMAND_BAR_EXCHANGE_RATES_H_
#define UW_BROWSER_UI_WEBUI_UW_COMMAND_BAR_EXCHANGE_RATES_H_

#include <memory>
#include <optional>
#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/supports_user_data.h"
#include "base/time/time.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "url/gurl.h"
#include "uw/browser/ui/webui/uw_command_bar/uw_command_bar.mojom.h"

class Profile;

namespace network {
class SimpleURLLoader;
}

namespace uw {

// The currency of the Mac's region, or USD.
std::string RegionCurrency();

// Keeps a profile's copy of the European Central Bank's daily reference
// rates in its prefs. It downloads them when the command bar asks and the
// copy is a day old, and keeps the copy when a download fails. The request
// carries nothing the user typed.
class ExchangeRates : public base::SupportsUserData::Data {
 public:
  // Made the first time a command bar asks.
  static ExchangeRates* For(Profile* profile);
  // Where tests serve the rates from.
  static void SetSourceForTesting(const GURL& url);

  ExchangeRates(const ExchangeRates&) = delete;
  ExchangeRates& operator=(const ExchangeRates&) = delete;
  ~ExchangeRates() override;

  // The last rates downloaded, or null before the first download.
  mojom::ExchangeRatesPtr Saved() const;
  // Downloads the day's rates unless the copy is less than a day old, a
  // download runs, or one was tried in the last hour.
  void Refresh();

 private:
  explicit ExchangeRates(Profile* profile);

  void OnDownloaded(std::optional<std::string> body);
  void OnParsed(data_decoder::DataDecoder::ValueOrError result);

  const raw_ptr<Profile> profile_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  base::TimeTicks tried_;
  base::WeakPtrFactory<ExchangeRates> weak_factory_{this};
};

}  // namespace uw

#endif  // UW_BROWSER_UI_WEBUI_UW_COMMAND_BAR_EXCHANGE_RATES_H_
