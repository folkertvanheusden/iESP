Name:       iesp
Version:    3.0
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
* Tue Nov 24 2026 Folkert van Heusden <folkert@vanheusden.com>
-
