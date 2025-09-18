# BzPeri Debian 패키지 생성 및 배포 가이드

이 가이드는 BzPeri를 Debian 패키지(.deb)로 빌드하고 APT 저장소에서 설치 가능하게 만드는 방법을 설명합니다.

## 📋 목차

1. [패키지 구조](#패키지-구조)
2. [빌드 방법](#빌드-방법)
3. [로컬 APT 저장소 설정](#로컬-apt-저장소-설정)
4. [패키지 설치 및 사용](#패키지-설치-및-사용)
5. [공식 저장소 배포](#공식-저장소-배포)

## 📦 패키지 구조

BzPeri는 다음 3개의 Debian 패키지로 분리되며, **amd64**와 **arm64** 아키텍처를 지원합니다:

### `bzperi` (런타임 라이브러리)
- **설명**: BzPeri 런타임 라이브러리
- **포함 파일**: `libbzp.so.*`
- **의존성**: `libglib2.0-0`, `libgio-2.0-0`, `libgobject-2.0-0`, `bluez`

### `bzperi-dev` (개발 파일)
- **설명**: BzPeri 개발용 헤더 파일 및 정적 라이브러리
- **포함 파일**: 헤더 파일, `libbzp.so`, `bzperi.pc`
- **의존성**: `bzperi`, 개발 라이브러리들

### `bzperi-tools` (명령줄 도구)
- **설명**: BzPeri 테스트 및 데모용 도구
- **포함 파일**: `bzp-standalone`
- **의존성**: `bzperi`

## 🔨 빌드 방법

### 1. 시스템 요구사항

```bash
# Ubuntu/Debian 시스템에서 실행
sudo apt update
sudo apt install build-essential cmake pkg-config debhelper \
    libglib2.0-dev libgio-2.0-dev libgobject-2.0-dev \
    libbluetooth-dev bluez bluez-tools
```

### 2. 자동 빌드 (권장)

편리한 빌드 스크립트를 사용:

```bash
# 실행 권한 부여
chmod +x scripts/build-deb.sh

# CPack을 이용한 빌드 (기본 - amd64)
./scripts/build-deb.sh

# 특정 아키텍처 빌드
./scripts/build-deb.sh --arch amd64    # x86_64 시스템용
./scripts/build-deb.sh --arch arm64    # ARM64 크로스 컴파일

# 네이티브 Debian 도구를 이용한 빌드
./scripts/build-deb.sh --native

# 빌드 후 설치 테스트 (sudo 필요)
./scripts/build-deb.sh --test-install
```

### 3. 수동 빌드

#### 방법 A: CMake + CPack

```bash
# 빌드 디렉토리 생성
mkdir build-deb && cd build-deb

# 설정
cmake .. \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STANDALONE=ON \
    -DENABLE_BLUEZ_ADVANCED=ON \
    -DENABLE_PERFORMANCE_OPTIMIZATION=ON

# 빌드
make -j$(nproc)

# 패키지 생성
cpack -G DEB
```

#### 방법 B: Debian 네이티브 도구

```bash
# debian/rules 실행 권한 부여
chmod +x debian/rules

# 소스 패키지 빌드
dpkg-source -b .

# 바이너리 패키지 빌드
dpkg-buildpackage -us -uc -b
```

### 4. 빌드 결과 확인

빌드가 완료되면 `packages/` 디렉토리에 다음 파일들이 생성됩니다:

```
packages/
├── bzperi_1.0.0-1_amd64.deb              # 런타임 라이브러리
├── bzperi-dev_1.0.0-1_amd64.deb          # 개발 파일
├── bzperi-tools_1.0.0-1_amd64.deb        # 명령줄 도구
├── bzperi_1.0.0-1_amd64.changes          # 변경사항 (네이티브 빌드 시)
└── bzperi_1.0.0-1_amd64.buildinfo        # 빌드 정보 (네이티브 빌드 시)
```

## 🏪 로컬 APT 저장소 설정

로컬 APT 저장소를 만들어 `apt install`로 설치할 수 있게 만듭니다.

### 1. 자동 설정 (권장)

```bash
# 실행 권한 부여
chmod +x scripts/setup-apt-repo.sh

# 완전한 APT 저장소 설정 (GPG 서명 포함)
sudo ./scripts/setup-apt-repo.sh

# GPG 서명 없이 설정 (개발용)
sudo ./scripts/setup-apt-repo.sh --skip-gpg

# 저장소만 생성 (APT 설정 안함)
sudo ./scripts/setup-apt-repo.sh --no-configure
```

### 2. 수동 설정

```bash
# 저장소 디렉토리 생성
sudo mkdir -p /var/local/bzperi-repo/{pool/main,dists/stable/main/binary-amd64}

# 패키지 복사
sudo cp packages/*.deb /var/local/bzperi-repo/pool/main/

# Packages 파일 생성
cd /var/local/bzperi-repo
sudo dpkg-scanpackages pool/main /dev/null | gzip -9c > dists/stable/main/binary-amd64/Packages.gz
sudo dpkg-scanpackages pool/main /dev/null > dists/stable/main/binary-amd64/Packages

# Release 파일 생성
cd dists/stable
sudo apt-ftparchive release . > Release

# APT 소스 추가
echo "deb [trusted=yes] file:///var/local/bzperi-repo stable main" | sudo tee /etc/apt/sources.list.d/bzperi-local.list

# APT 캐시 업데이트
sudo apt update
```

## 💾 패키지 설치 및 사용

### 1. APT를 통한 설치

```bash
# APT 캐시 업데이트
sudo apt update

# 모든 패키지 설치
sudo apt install bzperi bzperi-dev bzperi-tools

# 또는 개별 설치
sudo apt install bzperi          # 런타임만
sudo apt install bzperi-dev      # 개발 파일 (런타임 포함)
sudo apt install bzperi-tools    # 도구 (런타임 포함)
```

### 2. 직접 설치

```bash
# 의존성 순서대로 설치
sudo dpkg -i packages/bzperi_*.deb
sudo dpkg -i packages/bzperi-dev_*.deb
sudo dpkg -i packages/bzperi-tools_*.deb

# 의존성 문제 해결 (필요시)
sudo apt-get install -f
```

### 3. 설치 확인

```bash
# 라이브러리 확인
ldconfig -p | grep bzp

# 헤더 파일 확인
ls /usr/include/BzPeri.h

# 도구 확인
which bzp-standalone
bzp-standalone --help

# pkg-config 확인
pkg-config --cflags --libs bzperi

# D-Bus 정책 파일 확인
ls /etc/dbus-1/system.d/com.bzperi.conf

# BlueZ 설정 헬퍼 스크립트 확인
ls /usr/share/bzperi/configure-bluez-experimental.sh

# BlueZ experimental 모드 설정 (권장)
sudo /usr/share/bzperi/configure-bluez-experimental.sh enable

# D-Bus 정책 적용 확인 (설치 후 자동으로 적용됨)
sudo systemctl status dbus
```

### 4. 개발용 사용

```bash
# pkg-config를 이용한 컴파일
gcc $(pkg-config --cflags bzperi) main.c $(pkg-config --libs bzperi) -o main

# CMake 프로젝트에서 사용
find_package(PkgConfig REQUIRED)
pkg_check_modules(BZPERI REQUIRED bzperi)

target_link_libraries(your_app ${BZPERI_LIBRARIES})
target_include_directories(your_app PRIVATE ${BZPERI_INCLUDE_DIRS})
```

### 5. 도구 사용

```bash
# 사용 가능한 BlueZ 어댑터 확인
sudo bzp-standalone --list-adapters

# 데모 서버 실행
sudo bzp-standalone -d

# 특정 어댑터 사용
sudo bzp-standalone --adapter=hci1 -d
```

## 🌐 공식 저장소 배포

### GitHub Pages를 APT 저장소로 사용

이 저장소에는 GitHub Actions 워크플로(`.github/workflows/apt-publish.yml`)가 포함되어 태그/릴리스 시 자동으로 APT 저장소를 생성하여 GitHub Pages로 배포합니다.

1) GitHub Pages 활성화: Settings → Pages → Source를 “GitHub Actions”로 설정

2) GPG 비밀키 등록 (선택사항이지만 권장)
- Settings → Secrets and variables → Actions → New repository secret
- `APT_GPG_PRIVATE_KEY`: ASCII-armored 개인키 (예: `gpg --armor --export-secret-keys KEYID`)
- `APT_GPG_PASSPHRASE`: 키 비밀번호(없으면 비워둠)

3) 릴리스 트리거
- 태그 생성: `git tag -a v1.0.0 -m "v1.0.0" && git push origin v1.0.0`
- 또는 Release publish

4) 사용자 설치 안내
```bash
# 공개키 등록 (GitHub Pages 경로 기준)
curl -fsSL https://<USER>.github.io/<REPO>/repo/repo.key | sudo gpg --dearmor -o /usr/share/keyrings/bzperi-archive-keyring.gpg

# APT 소스 추가
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bzperi-archive-keyring.gpg] https://<USER>.github.io/<REPO>/repo stable main" | \
  sudo tee /etc/apt/sources.list.d/bzperi.list

sudo apt update
sudo apt install bzperi bzperi-dev bzperi-tools
```

### 1. GitHub Releases

```bash
# 릴리스 태그 생성
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0

# GitHub CLI로 릴리스 생성
gh release create v1.0.0 packages/*.deb \
    --title "BzPeri v1.0.0" \
    --notes "First stable release of BzPeri"
```

### 2. PPA (Personal Package Archive) 생성

Ubuntu PPA를 통한 배포:

```bash
# Launchpad에 PPA 생성 후
dput ppa:your-username/bzperi ../libbzperi_1.0.0-1_source.changes
```

### 3. 공식 Debian/Ubuntu 저장소

공식 저장소 등록을 위한 단계:

1. **Debian**: [debian-mentors](https://mentors.debian.net/)에 패키지 업로드
2. **Ubuntu**: REVU 프로세스를 통한 검토 요청
3. **ITP (Intent To Package)** 버그 리포트 제출

## 🔧 문제 해결

### 빌드 오류

```bash
# 의존성 누락
sudo apt install build-essential cmake pkg-config debhelper

# GLib 개발 파일 누락
sudo apt install libglib2.0-dev libgio-2.0-dev libgobject-2.0-dev

# BlueZ 개발 파일 누락
sudo apt install libbluetooth-dev bluez
```

### 패키지 설치 오류

```bash
# 의존성 문제 해결
sudo apt-get install -f

# 강제 설치 (권장하지 않음)
sudo dpkg -i --force-depends package.deb
```

### D-Bus 권한 문제

일반적으로 D-Bus 정책은 자동으로 적용되지만, 문제가 있는 경우:

```bash
# D-Bus 정책 파일 확인
ls -la /etc/dbus-1/system.d/com.bzperi.conf

# D-Bus 수동 재시작 (문제 해결용, 일반적으로 불필요)
sudo systemctl reload dbus

# 또는 전체 재시작 (최후 수단)
sudo systemctl restart dbus

# 권한 테스트
sudo bzp-standalone --list-adapters
```

### 저장소 문제

```bash
# APT 캐시 정리
sudo apt clean && sudo apt update

# 저장소 제거
sudo rm /etc/apt/sources.list.d/bzperi-local.list
sudo apt update
```

## 📝 추가 정보

- **라이선스**: MIT License (원본 Gobbledegook은 BSD-style)
- **지원 플랫폼**: Linux (BlueZ 5.42+, 권장: 5.77+)
- **C++ 표준**: C++20
- **GitHub**: https://github.com/jy1655/BzPeri

패키지 관련 문의사항은 GitHub Issues에 등록해 주세요.
