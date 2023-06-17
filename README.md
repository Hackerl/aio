<!-- Improved compatibility of back to top link: See: https://github.com/othneildrew/Best-README-Template/pull/73 -->
<a name="readme-top"></a>
<!--
*** Thanks for checking out the Best-README-Template. If you have a suggestion
*** that would make this better, please fork the repo and create a pull request
*** or simply open an issue with the tag "enhancement".
*** Don't forget to give the project a star!
*** Thanks again! Now go create something AMAZING! :D
-->



<!-- PROJECT SHIELDS -->
<!--
*** I'm using markdown "reference style" links for readability.
*** Reference links are enclosed in brackets [ ] instead of parentheses ( ).
*** See the bottom of this document for the declaration of the reference variables
*** for contributors-url, forks-url, etc. This is an optional, concise syntax you may use.
*** https://www.markdownguide.org/basic-syntax/#reference-style-links
-->
[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![Apache 2.0 License][license-shield]][license-url]



<!-- PROJECT LOGO -->
<br />
<div align="center">

<h3 align="center">AIO</h3>

  <p align="center">
    A promise-based asynchronous library implemented in C++17
    <br />
    <a href="https://github.com/Hackerl/aio/wiki"><strong>Explore the docs »</strong></a>
    <br />
    <br />
    <a href="https://github.com/Hackerl/aio/tree/master/sample">View Demo</a>
    ·
    <a href="https://github.com/Hackerl/aio/issues">Report Bug</a>
    ·
    <a href="https://github.com/Hackerl/aio/issues">Request Feature</a>
  </p>
</div>



<!-- TABLE OF CONTENTS -->
<details>
  <summary>Table of Contents</summary>
  <ol>
    <li>
      <a href="#about-the-project">About The Project</a>
      <ul>
        <li><a href="#built-with">Built With</a></li>
      </ul>
    </li>
    <li>
      <a href="#getting-started">Getting Started</a>
      <ul>
        <li><a href="#prerequisites">Prerequisites</a></li>
        <li><a href="#build">Build</a></li>
      </ul>
    </li>
    <li><a href="#usage">Usage</a></li>
    <li><a href="#roadmap">Roadmap</a></li>
    <li><a href="#contributing">Contributing</a></li>
    <li><a href="#license">License</a></li>
    <li><a href="#contact">Contact</a></li>
    <li><a href="#acknowledgments">Acknowledgments</a></li>
  </ol>
</details>



<!-- ABOUT THE PROJECT -->
## About The Project

Wrap `libevent` with `promise`, asynchronize basic components such as timer, based on `bufferevent` and `libcurl` implement `network` asynchronous api, and provide `channel` to send and receive data between tasks.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



### Built With

* [![CMake][CMake]][CMake-url]
* [![vcpkg][vcpkg]][vcpkg-url]
* [![C++17][C++17]][C++17-url]

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- GETTING STARTED -->
## Getting Started

Due to many dependencies, it is not recommended to install manually, you should use `vcpkg`.

### Prerequisites

Create a `CMake` project and two configuration files, `vcpkg-configuration.json` and `vcpkg.json`.

* CMakeLists.txt
  ```cmake
  find_package(aio CONFIG REQUIRED)
  add_executable(demo main.cpp)
  target_link_libraries(demo PRIVATE aio::aio)
  ```

* vcpkg-configuration.json
  ```json
  {
    "registries": [
      {
        "kind": "git",
        "repository": "https://github.com/Hackerl/vcpkg-registry",
        "baseline": "7cc1a197fc00fbc0637e20776187b256b4d2454c",
        "packages": [
          "aio",
          "zero"
        ]
      }
    ]
  }
  ```

* vcpkg.json
  ```json
  {
    "name": "demo",
    "version-string": "1.0.0",
    "builtin-baseline": "69efe9cc2df0015f0bb2d37d55acde4a75c9a25b",
    "dependencies": [
      {
        "name": "aio",
        "version>=": "1.0.5"
      },
      {
        "name": "curl",
        "default-features": false
      }
    ]
  }
  ```

Export environment variables:
* VCPKG_INSTALLATION_ROOT
* ANDROID_NDK_HOME(Android)

### Build

* Linux
  ```sh
  mkdir -p build && cmake -B build -DCMAKE_TOOLCHAIN_FILE="${VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake" && cmake --build build -j$(nproc)
  ```

* Android
  ```sh
  # set "ANDROID_PLATFORM" for dependencies installed by vcpkg: echo 'set(VCPKG_CMAKE_SYSTEM_VERSION 24)' >> "${VCPKG_INSTALLATION_ROOT}/triplets/community/arm64-android.cmake"
  mkdir -p build && cmake -B build -DCMAKE_TOOLCHAIN_FILE="${VCPKG_INSTALLATION_ROOT}/scripts/buildsystems/vcpkg.cmake" -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" -DVCPKG_TARGET_TRIPLET=arm64-android -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 && cmake --build build -j$(nproc)
  ```

* Windows(Developer PowerShell)
  ```sh
  mkdir -p build && cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" && cmake --build build -j $env:NUMBER_OF_PROCESSORS
  ```

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- USAGE EXAMPLES -->
## Usage

### HTTP Requests

* Basic

  ```cpp
  #include <aio/http/request.h>
  #include <zero/log.h>

  int main() {
      INIT_CONSOLE_LOG(zero::INFO_LEVEL);

  #ifdef _WIN32
      WSADATA wsaData;

      if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
          LOG_ERROR("WSAStartup failed");
          return -1;
      }
  #endif

      std::shared_ptr<aio::Context> context = aio::newContext();

      if (!context)
          return -1;

      zero::ptr::RefPtr<aio::http::Requests> requests = zero::ptr::makeRef<aio::http::Requests>(context);

      requests->get("https://www.google.com")->then([=](const zero::ptr::RefPtr<aio::http::Response> &response) {
          return response->string();
      })->then([](const std::string &content) {
          LOG_INFO("content: %s", content.c_str());
      })->fail([](const zero::async::promise::Reason &reason) {
          LOG_ERROR("%s", reason.message.c_str());
      })->finally([=]() {
          context->loopBreak();
      });

      context->dispatch();

  #ifdef _WIN32
      WSACleanup();
  #endif

      return 0;
  }
  ```

* Post form

  ```cpp
  requests->post(
          "https://www.google.com",
          std::map<std::string, std::string>{
                  {"name", "jack"}
          }
  );
  ```

* Post file

  ```cpp
  requests->post(
          "https://www.google.com",
          std::map<std::string, std::filesystem::path>{
                  {"file", "/tmp/test"}
          }
  );

  requests->post(
          "https://www.google.com",
          std::map<std::string, std::variant<std::string, std::filesystem::path>>{
                  {"name", std::string{"jack"}},
                  {"file", std::filesystem::path{"/tmp/test"}}
          }
  );
  ```

* Post json

  ```cpp
  requests->post(
          "https://www.google.com",
          nlohmann::json{
                  {"name", "jack"}
          }
  )->then([=](const zero::ptr::RefPtr<aio::http::Response> &response) {
      return response->json();
  })->then([](const nlohmann::json &j) {

  });

  requests->post(
          "https://www.google.com",
          nlohmann::json{
                  {"name", "jack"}
          }
  )->then([=](const zero::ptr::RefPtr<aio::http::Response> &response) {
      return response->json<People>();
  })->then([](const People &people) {

  });
  ```

* Websocket

  ```cpp
  aio::http::ws::connect(context, url)->then([](const zero::ptr::RefPtr<aio::http::ws::WebSocket> &ws) {
      return zero::async::promise::loop<void>([=](const auto &loop) {
          ws->read()->then([=](const aio::http::ws::Message &message) {
              switch (message.opcode) {
                  case aio::http::ws::TEXT:
                      LOG_INFO("receive text message: %s", std::get<std::string>(message.data).c_str());
                      break;

                  case aio::http::ws::BINARY: {
                      const auto &binary = std::get<std::vector<std::byte>>(message.data);
                      LOG_INFO(
                              "receive binary message: %s",
                              zero::encoding::hex::encode(binary).c_str()
                      );
                      break;
                  }

                  default:
                      break;
              }

              return ws->write(message);
          })->then([=]() {
              P_CONTINUE(loop);
          }, [=](const zero::async::promise::Reason &reason) {
              P_BREAK_E(loop, reason);
          });
      });
  })
  ```

### TCP/TLS

* Basic

  ```cpp
  aio::net::connect(context, "www.google.com", 80)->then([=](const zero::ptr::RefPtr<aio::ev::IBuffer> &buffer) {
      buffer->writeLine("hello world");
      return buffer->drain()->then([=]() {
          return buffer->read();
      });
  })->then([](nonstd::span<const std::byte> data) {

  });
  ```

* TLS

  ```cpp
  aio::net::ssl::connect(context, "www.google.com", 443)->then([=](const zero::ptr::RefPtr<aio::ev::IBuffer> &buffer) {
      buffer->writeLine("hello world");
      return buffer->drain()->then([=]() {
          return buffer->read();
      });
  })->then([](nonstd::span<const std::byte> data) {

  });
  ```

### Worker

* Basic

  ```cpp
  aio::toThread<int>(context, []() {
      std::this_thread::sleep_for(100ms);
      return 1024;
  })->then([=](int result) {
      REQUIRE(result == 1024);
  });
  ```

* Throw error

  ```cpp
  aio::toThread<int>(context, []() -> nonstd::expected<int, zero::async::promise::Reason> {
      std::this_thread::sleep_for(100ms);
      return nonstd::make_unexpected(zero::async::promise::Reason{-1});
  })->then([=](int) {
      FAIL();
  }, [=](const zero::async::promise::Reason &reason) {
      REQUIRE(reason.code == -1);
      context->loopBreak();
  });
  ```

### Channel

* Basic

  ```cpp
  zero::ptr::RefPtr<aio::IChannel<int>> channel = zero::ptr::makeRef<aio::Channel<int, 100>>(context);

  zero::async::promise::all(
          zero::ptr::makeRef<aio::ev::Timer>(context)->setInterval(5s, [=]() {
              channel->trySend(1024);
              return true;
          }),
          aio::toThread<void>(context, [=]() {
              while (true) {
                  std::optional<int> element = channel->receiveSync();

                  if (!element)
                      break;

                  // do something that takes a long time
              }
          })
  );
  ```

_For more examples, please refer to the [Documentation](https://github.com/Hackerl/aio/wiki)_

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- ROADMAP -->
## Roadmap

See the [open issues](https://github.com/Hackerl/aio/issues) for a full list of proposed features (and known issues).

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- CONTRIBUTING -->
## Contributing

Contributions are what make the open source community such an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

If you have a suggestion that would make this better, please fork the repo and create a pull request. You can also simply open an issue with the tag "enhancement".
Don't forget to give the project a star! Thanks again!

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- LICENSE -->
## License

Distributed under the Apache 2.0 License. See `LICENSE` for more information.

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- CONTACT -->
## Contact

Hackerl - [@Hackerl](https://github.com/Hackerl) - patteliu@gmail.com

Project Link: [https://github.com/Hackerl/aio](https://github.com/Hackerl/aio)

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- ACKNOWLEDGMENTS -->
## Acknowledgments

* [libevent](https://libevent.org)
* [libcurl](https://curl.se/libcurl)
* [nlohmann-json](https://github.com/nlohmann/json)

<p align="right">(<a href="#readme-top">back to top</a>)</p>



<!-- MARKDOWN LINKS & IMAGES -->
<!-- https://www.markdownguide.org/basic-syntax/#reference-style-links -->
[contributors-shield]: https://img.shields.io/github/contributors/Hackerl/aio.svg?style=for-the-badge
[contributors-url]: https://github.com/Hackerl/aio/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/Hackerl/aio.svg?style=for-the-badge
[forks-url]: https://github.com/Hackerl/aio/network/members
[stars-shield]: https://img.shields.io/github/stars/Hackerl/aio.svg?style=for-the-badge
[stars-url]: https://github.com/Hackerl/aio/stargazers
[issues-shield]: https://img.shields.io/github/issues/Hackerl/aio.svg?style=for-the-badge
[issues-url]: https://github.com/Hackerl/aio/issues
[license-shield]: https://img.shields.io/github/license/Hackerl/aio.svg?style=for-the-badge
[license-url]: https://github.com/Hackerl/aio/blob/master/LICENSE
[CMake]: https://img.shields.io/badge/CMake-000000?style=for-the-badge&logo=cmake&logoColor=FF3E00
[CMake-url]: https://cmake.org
[vcpkg]: https://img.shields.io/badge/vcpkg-000000?style=for-the-badge&logo=microsoft&logoColor=61DAFB
[vcpkg-url]: https://vcpkg.io
[C++17]: https://img.shields.io/badge/C++17-000000?style=for-the-badge&logo=cplusplus&logoColor=4FC08D
[C++17-url]: https://en.cppreference.com/w/cpp/17