// Copyright 2026 The uw Authors. All rights reserved.

#include "uw/browser/translation/google_cloud_translator.h"

#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "uw/browser/ai_providers/api_key_store.h"
#include "url/gurl.h"

namespace uw {

namespace {

constexpr char kEndpoint[] =
    "https://translation.googleapis.com/language/translate/v2";
// Points the requests at a test server.
constexpr char kEndpointSwitch[] = "uw-google-cloud-translation-url";
constexpr base::TimeDelta kTimeout = base::Seconds(30);

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("uw_google_cloud_translation", R"(
      semantics {
        sender: "uw page translation"
        description:
          "Translates the text of a page the user asked to translate, with "
          "Google Cloud Translation and the user's own API key."
        trigger:
          "The user translates a page while Google Cloud Translation is the "
          "translation engine, or saves a key, which translates one word to "
          "check it."
        data:
          "The page's text, its language, the language to translate it into, "
          "and the user's API key."
        destination: GOOGLE_OWNED_SERVICE
      }
      policy {
        cookies_allowed: NO
        setting:
          "Settings, Languages, Translate pages with. Nothing is sent unless "
          "the user picks Google Cloud Translation."
        policy_exception_justification: "Not implemented."
      })");

GURL Endpoint() {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  return GURL(command_line.HasSwitch(kEndpointSwitch)
                  ? command_line.GetSwitchValueASCII(kEndpointSwitch)
                  : kEndpoint);
}

std::unique_ptr<network::SimpleURLLoader> Request(
    const std::string& key,
    const std::vector<std::string>& segments,
    const std::string& source_language,
    const std::string& target_language) {
  base::ListValue texts;
  for (const std::string& segment : segments) {
    texts.Append(segment);
  }
  base::DictValue body;
  body.Set("q", std::move(texts));
  body.Set("target", target_language);
  body.Set("format", "html");
  if (!source_language.empty() && source_language != "auto" &&
      source_language != "und") {
    body.Set("source", source_language);
  }
  std::string json;
  base::JSONWriter::Write(body, &json);

  auto resource = std::make_unique<network::ResourceRequest>();
  resource->url = Endpoint();
  resource->method = "POST";
  resource->credentials_mode = network::mojom::CredentialsMode::kOmit;
  resource->headers.SetHeader("X-goog-api-key", key);
  auto loader =
      network::SimpleURLLoader::Create(std::move(resource), kTrafficAnnotation);
  loader->AttachStringForUpload(json, "application/json");
  loader->SetTimeoutDuration(kTimeout);
  loader->SetAllowHttpErrorResults(true);
  return loader;
}

// Google's translations of `count` segments, or its words about what went
// wrong.
SegmentTranslator::Result ReadResponse(const network::SimpleURLLoader& loader,
                                       const std::optional<std::string>& body,
                                       size_t count) {
  std::optional<base::DictValue> json =
      body ? base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC)
           : std::nullopt;
  const int status = loader.ResponseInfo() && loader.ResponseInfo()->headers
                         ? loader.ResponseInfo()->headers->response_code()
                         : 0;
  if (json && status == 200) {
    const base::ListValue* translations =
        json->FindListByDottedPath("data.translations");
    if (translations && translations->size() == count) {
      SegmentTranslator::Result result;
      for (const base::Value& translation : *translations) {
        const std::string* text =
            translation.is_dict()
                ? translation.GetDict().FindString("translatedText")
                : nullptr;
        result.translations.push_back(text ? *text : std::string());
      }
      return result;
    }
  }
  if (const std::string* message =
          json ? json->FindStringByDottedPath("error.message") : nullptr) {
    return SegmentTranslator::Result::Failed(*message);
  }
  if (status) {
    return SegmentTranslator::Result::Failed("HTTP " +
                                             base::NumberToString(status));
  }
  return SegmentTranslator::Result::Failed(
      net::ErrorToShortString(loader.NetError()));
}

}  // namespace

// static
void GoogleCloudTranslator::CheckKey(
    scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
    const std::string& key,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  auto translator =
      std::make_unique<GoogleCloudTranslator>(std::move(loader_factory), key);
  GoogleCloudTranslator* raw = translator.get();
  raw->Translate(
      {"Hello"}, "en", "es",
      base::BindOnce(
          [](std::unique_ptr<GoogleCloudTranslator> translator,
             base::OnceCallback<void(std::optional<std::string>)> callback,
             Result result) {
            std::move(callback).Run(
                result.problem == Result::Problem::kNone
                    ? std::nullopt
                    : std::optional<std::string>(result.message));
          },
          std::move(translator), std::move(callback)));
}

GoogleCloudTranslator::GoogleCloudTranslator(
    scoped_refptr<network::SharedURLLoaderFactory> loader_factory,
    std::optional<std::string> key)
    : loader_factory_(std::move(loader_factory)), key_(std::move(key)) {}

GoogleCloudTranslator::~GoogleCloudTranslator() = default;

void GoogleCloudTranslator::Translate(std::vector<std::string> segments,
                                      const std::string& source_language,
                                      const std::string& target_language,
                                      ResultCallback callback) {
  if (segments.empty()) {
    std::move(callback).Run(Result());
    return;
  }
  const size_t count = segments.size();
  loaders_.push_front(
      Request(key_ ? *key_
                   : ApiKeyStore().GetKey(kKeyAccount).value_or(std::string()),
              segments, source_language, target_language));
  Loaders::iterator loader = loaders_.begin();
  (*loader)->DownloadToString(
      loader_factory_.get(),
      base::BindOnce(&GoogleCloudTranslator::OnResponse,
                     weak_factory_.GetWeakPtr(), loader, count,
                     std::move(callback)),
      network::SimpleURLLoader::kMaxBoundedStringDownloadSize);
}

void GoogleCloudTranslator::OnResponse(Loaders::iterator loader,
                                       size_t count,
                                       ResultCallback callback,
                                       std::optional<std::string> body) {
  Result result = ReadResponse(**loader, body, count);
  loaders_.erase(loader);
  std::move(callback).Run(std::move(result));
}

}  // namespace uw
