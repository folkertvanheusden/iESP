Name:       iesp
Version:    2.2
Release:    0
Summary:    A simple iSCSI target server application.
License:    MIT
Source0:    %{name}-%{version}.tgz
URL:        https://github.com/folkertvanheusden/iesp

%description
A simple iSCSI target server application.

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
* Sun Nov 03 2024 Folkert van Heusden <mail@vanheusden.com>
-
