Name:           gridex
Version:        0.1.0
Release:        1%{?dist}
Summary:        AI-Native Database IDE
License:        MIT
URL:            https://github.com/vurakit/vura

BuildRequires:  cmake >= 3.20
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel
BuildRequires:  libpq-devel
BuildRequires:  mariadb-connector-c-devel
BuildRequires:  libssh-devel
BuildRequires:  libsecret-devel
BuildRequires:  hiredis-devel
BuildRequires:  freetds-devel
BuildRequires:  unixODBC-devel
BuildRequires:  pkgconf-pkg-config
BuildRequires:  mesa-libGL-devel

Requires:       qt6-qtbase
Requires:       libpq
Requires:       mariadb-connector-c
Requires:       libssh
Requires:       libsecret
Requires:       hiredis

%description
Gridex is an AI-native database IDE for Linux that provides a powerful
GUI for managing multiple database backends including PostgreSQL, MySQL,
SQLite, MongoDB, Redis, and SQL Server, combined with AI chat and an
MCP server for external AI agent integration.

%prep
# Sources are checked out directly; no tarball extraction needed in CI.
# When building locally with rpmbuild, copy sources into
# %{_builddir}/%{name}-%{version}/ before invoking this spec.

%build
cmake -S . -B %{_builddir}/%{name}-%{version}/build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix}
cmake --build %{_builddir}/%{name}-%{version}/build --parallel

%install
DESTDIR=%{buildroot} cmake --install %{_builddir}/%{name}-%{version}/build

%files
%license LICENSE
%{_bindir}/gridex

%changelog
* Tue Apr 15 2026 Gridex Team <danghuynh.dev@gmail.com> - 0.1.0-1
- Initial release
