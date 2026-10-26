// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/ui/webui/uw_command_bar/exchange_rates.h"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <utility>
#include <vector>

#include "base/apple/foundation_util.h"
#include "base/apple/scoped_cftyperef.h"
#include "base/functional/bind.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/storage_partition.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/data_decoder/public/cpp/safe_xml_parser.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "uw/browser/tab_organization/ai_connection.h"

namespace uw {
namespace {

constexpr char kUserDataKey[] = "uw_exchange_rates";
constexpr base::TimeDelta kRefreshAfter = base::Days(1);
constexpr base::TimeDelta kRetryAfter = base::Hours(1);
constexpr base::TimeDelta kTimeout = base::Seconds(30);
// The daily file was about 1.8 KB on 2026-10-19.
constexpr size_t kMaxBytes = 64 * 1024;

GURL& Source() {
  static base::NoDestructor<GURL> source(
      GURL("https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml"));
  return *source;
}

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_exchange_rates", R"(
      semantics {
        sender: "uw command bar"
        description:
          "Downloads the European Central Bank's daily euro reference rates, "
          "so the command bar can convert currencies on the Mac."
        trigger:
          "The command bar opens and the last rates are a day old."
        data: "None. The request carries no cookies and nothing the user typed."
        destination: OTHER
        destination_other: "The European Central Bank, www.ecb.europa.eu"
      }
      policy {
        cookies_allowed: NO
        setting: "None."
        policy_exception_justification: "Not implemented."
      })");

}  // namespace

std::string RegionCurrency() {
  base::apple::ScopedCFTypeRef<CFLocaleRef> locale(CFLocaleCopyCurrent());
  const CFStringRef code = base::apple::CFCast<CFStringRef>(
      CFLocaleGetValue(locale.get(), kCFLocaleCurrencyCode));
  return code ? base::SysCFStringRefToUTF8(code) : "USD";
}

// static
ExchangeRates* ExchangeRates::For(Profile* profile) {
  auto* rates = static_cast<ExchangeRates*>(profile->GetUserData(kUserDataKey));
  if (!rates) {
    std::unique_ptr<ExchangeRates> made =
        base::WrapUnique(new ExchangeRates(profile));
    rates = made.get();
    profile->SetUserData(kUserDataKey, std::move(made));
  }
  return rates;
}

// static
void ExchangeRates::SetSourceForTesting(const GURL& url) {
  Source() = url;
}

ExchangeRates::ExchangeRates(Profile* profile) : profile_(profile) {}

ExchangeRates::~ExchangeRates() = default;

mojom::ExchangeRatesPtr ExchangeRates::Saved() const {
  const base::DictValue& saved =
      profile_->GetPrefs()->GetDict(kCommandBarRatesPref);
  const base::DictValue* per_euro = saved.FindDict("per_euro");
  const std::string* date = saved.FindString("date");
  if (!per_euro || per_euro->empty() || !date) {
    return nullptr;
  }
  auto rates = mojom::ExchangeRates::New();
  rates->date = *date;
  for (const auto [code, rate] : *per_euro) {
    if (rate.is_double()) {
      rates->per_euro[code] = rate.GetDouble();
    }
  }
  return rates;
}

void ExchangeRates::Refresh() {
  const std::optional<double> fetched =
      profile_->GetPrefs()->GetDict(kCommandBarRatesPref).FindDouble("fetched");
  if (loader_ ||
      (fetched &&
       base::Time::Now() - base::Time::FromSecondsSinceUnixEpoch(*fetched) <
           kRefreshAfter) ||
      (!tried_.is_null() && base::TimeTicks::Now() - tried_ < kRetryAfter)) {
    return;
  }
  tried_ = base::TimeTicks::Now();
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = Source();
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  loader_ =
      network::SimpleURLLoader::Create(std::move(request), kTrafficAnnotation);
  loader_->SetTimeoutDuration(kTimeout);
  loader_->DownloadToString(
      profile_->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get(),
      base::BindOnce(&ExchangeRates::OnDownloaded, weak_factory_.GetWeakPtr()),
      kMaxBytes);
}

void ExchangeRates::OnDownloaded(std::optional<std::string> body) {
  if (!body) {
    loader_.reset();
    return;
  }
  // The loader stays until the file is read, so no second download starts.
  data_decoder::DataDecoder::ParseXmlIsolated(
      *body, data_decoder::mojom::XmlParser::WhitespaceBehavior::kIgnore,
      base::BindOnce(&ExchangeRates::OnParsed, weak_factory_.GetWeakPtr()));
}

// The file holds <Cube><Cube time="2026-10-19"><Cube currency="USD"
// rate="1.0850"/>… under its root.
void ExchangeRates::OnParsed(data_decoder::DataDecoder::ValueOrError result) {
  loader_.reset();
  if (!result.has_value()) {
    return;
  }
  const base::Value* all =
      data_decoder::GetXmlElementChildWithTag(*result, "Cube");
  const base::Value* day =
      all ? data_decoder::GetXmlElementChildWithTag(*all, "Cube") : nullptr;
  std::vector<const base::Value*> cubes;
  if (!day ||
      !data_decoder::GetAllXmlElementChildrenWithTag(*day, "Cube", &cubes)) {
    return;
  }
  base::DictValue per_euro;
  for (const base::Value* cube : cubes) {
    const std::string code =
        data_decoder::GetXmlElementAttribute(*cube, "currency");
    double rate = 0;
    if (code.size() == 3 && base::IsStringASCII(code) &&
        std::ranges::all_of(code, base::IsAsciiUpper<char>) &&
        base::StringToDouble(
            data_decoder::GetXmlElementAttribute(*cube, "rate"), &rate) &&
        rate > 0) {
      per_euro.Set(code, rate);
    }
  }
  const std::string date = data_decoder::GetXmlElementAttribute(*day, "time");
  if (per_euro.empty() || date.empty()) {
    return;
  }
  profile_->GetPrefs()->SetDict(
      kCommandBarRatesPref,
      base::DictValue()
          .Set("date", date)
          .Set("fetched", base::Time::Now().InSecondsFSinceUnixEpoch())
          .Set("per_euro", std::move(per_euro)));
}

}  // namespace uw
