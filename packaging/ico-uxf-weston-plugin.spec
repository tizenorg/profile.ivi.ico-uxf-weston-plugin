Name:       ico-uxf-weston-plugin
Summary:    Weston Plugins for IVI
Version:    0.7.01
Release:    1.1
Group:      System/GUI/Libraries
License:    MIT
URL:        ""
Source0:    %{name}-%{version}.tar.bz2

BuildRequires: pkgconfig(weston) >= 1.2
BuildRequires: pkgconfig(pixman-1)
BuildRequires: pkgconfig(xkbcommon) >= 0.0.578
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(evas)
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(elementary)
BuildRequires: pkgconfig(ecore-wayland)
BuildRequires: mesa-devel
BuildRequires: pkgconfig(aul)
BuildRequires: pkgconfig(ecore)
Requires: weston >= 1.2

%description
Weston Plugins for IVI

%package devel
Summary:    Development files for %{name}
Group:      Development/GUI/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
Development files that expose the wayland extended protocols for IVI.

%prep
%setup -q -n %{name}-%{version}

%build
autoreconf --install

%autogen --prefix=/usr

%configure
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%make_install

# configurations
%define weston_conf %{_sysconfdir}/xdg/weston
mkdir -p %{buildroot}%{weston_conf} > /dev/null 2>&1
install -m 0644 settings/weston.ini %{buildroot}%{weston_conf}
mkdir -p %{buildroot}%{_sysconfdir}/profile.d/
install -m 0644 settings/ico_weston.sh  %{buildroot}%{_sysconfdir}/profile.d/
install -m 0644 settings/ico_weston.csh  %{buildroot}%{_sysconfdir}/profile.d/

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%license COPYING
%dir %{_libdir}/weston/
%{_libdir}/weston/*.so
%{_libdir}/libico-uxf-weston-plugin.so.*
%{weston_conf}/weston.ini
%{_sysconfdir}/profile.d/ico_weston.sh
%{_sysconfdir}/profile.d/ico_weston.csh

%files devel
%defattr(-,root,root,-)
%{_includedir}/%{name}/desktop-shell-client-protocol.h
%{_includedir}/%{name}/input-method-client-protocol.h
%{_includedir}/%{name}/workspaces-client-protocol.h
%{_includedir}/%{name}/ico_input_mgr-client-protocol.h
%{_includedir}/%{name}/ico_window_mgr-client-protocol.h
%{_includedir}/%{name}/ico_input_mgr.h
%{_libdir}/libico-uxf-weston-plugin.so

