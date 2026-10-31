Name:       harbour-lampje
Summary:    Control Philips Hue lights
# Placeholder: release builds replace it with the version of the newest vX.Y.Z tag, which
# semantic-release creates. Local builds keep 0.1.0.
Version:    0.1.0
Release:    1%{?dev_suffix}
License:    AGPLv3+
URL:        https://github.com/readefries/sfos-hue
Source0:    %{name}-%{version}.tar.bz2

BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  desktop-file-utils
Requires:       sailfishsilica-qt5 >= 0.10.9

%description
Control Philips Hue lights from Sailfish OS: rooms and zones, single
lights with colour and colour temperature, and scenes. Talks directly to
the Hue bridge on your local network, with live updates when lights are
changed elsewhere.

%prep
%autosetup -n %{name}-%{version}

%build
%qmake5 VERSION=%{version}
%make_build

%check
make check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{name}.desktop || :

%install
%qmake5_install

%files
%license %{_datadir}/licenses/%{name}/LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
