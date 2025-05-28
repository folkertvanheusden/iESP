Name:       iesp
Version:    2.3
Release:    0
Summary:    An iSCSI target server application.
License:    MIT
Source0:    %{name}-%{version}.tgz
URL:        https://github.com/folkertvanheusden/iesp

%description
An iSCSI target server application.

%prep
%setup -q -n %{name}-%{version}

%build
%cmake .
%cmake_build

%install
%cmake_install

%files
/usr/bin/iesp

%changelog
* Wed May 28 2025 Folkert van Heusden <mail@vanheusden.com>
-
