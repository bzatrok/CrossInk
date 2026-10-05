#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "TrmnlProtocol.h"

namespace {

trmnl::DisplayResult display(const std::string& body) {
  trmnl::DisplayResult r{};
  trmnl::parseDisplay(body.c_str(), body.size(), r);
  return r;
}

// ---------------------------------------------------------------- setup

TEST(TrmnlSetup, ValidBodyCopiesKeyAndFriendlyId) {
  const std::string body = R"({"status":200,"api_key":"abc123","friendly_id":"F00BAR","image_url":"x","message":"ok"})";
  trmnl::SetupResult r{};
  ASSERT_TRUE(trmnl::parseSetup(body.c_str(), body.size(), r));
  EXPECT_TRUE(r.ok);
  EXPECT_STREQ(r.apiKey, "abc123");
  EXPECT_STREQ(r.friendlyId, "F00BAR");
}

TEST(TrmnlSetup, MissingKeyFails) {
  const std::string body = R"({"status":404,"message":"MAC address not registered"})";
  trmnl::SetupResult r{};
  EXPECT_FALSE(trmnl::parseSetup(body.c_str(), body.size(), r));
  EXPECT_FALSE(r.ok);
}

TEST(TrmnlSetup, TooLongKeyFails) {
  const std::string body = R"({"api_key":")" + std::string(64, 'k') + R"("})";
  trmnl::SetupResult r{};
  EXPECT_FALSE(trmnl::parseSetup(body.c_str(), body.size(), r));
  EXPECT_STREQ(r.apiKey, "");
}

TEST(TrmnlSetup, MalformedJsonFails) {
  const std::string body = "{not json";
  trmnl::SetupResult r{};
  EXPECT_FALSE(trmnl::parseSetup(body.c_str(), body.size(), r));
}

// ---------------------------------------------------------------- display

TEST(TrmnlDisplay, ValidBodyIgnoresRefreshRate) {
  const auto r = display(
      R"({"status":0,"image_url":"http://h:2300/assets/a.png","filename":"a.png","refresh_rate":900,"update_firmware":false})");
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(r.status, 0);
  EXPECT_STREQ(r.imageUrl, "http://h:2300/assets/a.png");
  EXPECT_STREQ(r.filename, "a.png");
}

TEST(TrmnlDisplay, MissingImageUrlParsesAsEmpty) {
  const auto r = display(R"({"status":0,"filename":"a.png"})");
  EXPECT_TRUE(r.ok);
  EXPECT_STREQ(r.imageUrl, "");
}

TEST(TrmnlDisplay, StatusValuesAreReported) {
  EXPECT_EQ(display(R"({"image_url":"u"})").status, 0);
  EXPECT_EQ(display(R"({"status":200,"image_url":"u"})").status, 200);
  EXPECT_EQ(display(R"({"status":202})").status, 202);
  EXPECT_EQ(display(R"({"status":500,"error":"boom"})").status, 500);
}

TEST(TrmnlDisplay, TooLongImageUrlFailsInsteadOfTruncating) {
  const std::string url = "http://h/" + std::string(260, 'a');
  const auto r = display(R"({"image_url":")" + url + R"("})");
  EXPECT_FALSE(r.ok);
  EXPECT_STREQ(r.imageUrl, "");
}

TEST(TrmnlDisplay, NonObjectBodyFails) {
  const std::string body = "[1,2]";
  trmnl::DisplayResult r{};
  EXPECT_FALSE(trmnl::parseDisplay(body.c_str(), body.size(), r));
}

// ---------------------------------------------------------------- no change

TEST(TrmnlNoChange, Status202IsNoChange) { EXPECT_TRUE(trmnl::isNoChange(display(R"({"status":202})"), "a.png")); }

TEST(TrmnlNoChange, SameFilenameIsNoChange) {
  EXPECT_TRUE(trmnl::isNoChange(display(R"({"status":0,"image_url":"u","filename":"a.png"})"), "a.png"));
}

TEST(TrmnlNoChange, NewOrEmptyFilenameIsAChange) {
  EXPECT_FALSE(trmnl::isNoChange(display(R"({"status":0,"image_url":"u","filename":"b.png"})"), "a.png"));
  EXPECT_FALSE(trmnl::isNoChange(display(R"({"status":0,"image_url":"u"})"), ""));
  EXPECT_FALSE(trmnl::isNoChange(display(R"({"status":0,"image_url":"u","filename":"a.png"})"), ""));
}

// ---------------------------------------------------------------- image URL

TEST(TrmnlImageUrl, AbsolutePassesThrough) {
  char out[64];
  ASSERT_TRUE(trmnl::resolveImageUrl("http://lan:2300", "https://cdn/x.png", out, sizeof(out)));
  EXPECT_STREQ(out, "https://cdn/x.png");
}

TEST(TrmnlImageUrl, RootRelativeJoinsOrigin) {
  char out[64];
  ASSERT_TRUE(trmnl::resolveImageUrl("http://lan:2300/base/", "/assets/x.png", out, sizeof(out)));
  EXPECT_STREQ(out, "http://lan:2300/assets/x.png");
}

TEST(TrmnlImageUrl, BareRelativeJoinsOrigin) {
  char out[64];
  ASSERT_TRUE(trmnl::resolveImageUrl("http://lan:2300", "assets/x.png", out, sizeof(out)));
  EXPECT_STREQ(out, "http://lan:2300/assets/x.png");
}

TEST(TrmnlImageUrl, OverflowFails) {
  char out[16];
  EXPECT_FALSE(trmnl::resolveImageUrl("http://lan:2300", "/assets/x.png", out, sizeof(out)));
  EXPECT_STREQ(out, "");
  EXPECT_FALSE(trmnl::resolveImageUrl("http://lan", "https://a-very-long-host/x.png", out, sizeof(out)));
}

TEST(TrmnlImageUrl, BadInputsFail) {
  char out[64];
  EXPECT_FALSE(trmnl::resolveImageUrl("lan:2300", "/x.png", out, sizeof(out)));
  EXPECT_FALSE(trmnl::resolveImageUrl("http://", "/x.png", out, sizeof(out)));
  EXPECT_FALSE(trmnl::resolveImageUrl("http://lan", "", out, sizeof(out)));
}

TEST(TrmnlFirmwareVersion, BuildSuffixIsDropped) {
  char out[24];
  ASSERT_TRUE(trmnl::firmwareVersionCore("1.6.1-x4-pro", out, sizeof(out)));
  EXPECT_STREQ(out, "1.6.1");
  ASSERT_TRUE(trmnl::firmwareVersionCore("10.20.300", out, sizeof(out)));
  EXPECT_STREQ(out, "10.20.300");
}

TEST(TrmnlFirmwareVersion, NonSemverFails) {
  char out[24];
  EXPECT_FALSE(trmnl::firmwareVersionCore("1.6", out, sizeof(out)));
  EXPECT_STREQ(out, "");
  EXPECT_FALSE(trmnl::firmwareVersionCore("v1.6.1", out, sizeof(out)));
  EXPECT_FALSE(trmnl::firmwareVersionCore("1..1", out, sizeof(out)));
  EXPECT_FALSE(trmnl::firmwareVersionCore(nullptr, out, sizeof(out)));
}

TEST(TrmnlFirmwareVersion, TooSmallBufferFails) {
  char out[5];
  EXPECT_FALSE(trmnl::firmwareVersionCore("1.6.1", out, sizeof(out)));
  EXPECT_STREQ(out, "");
}

}  // namespace
