%define weston_conf %{_sysconfdir}/xdg/weston

Name:       ico-uxf-weston-plugin
Summary:    Weston Plugins for IVI
Version:    0.9.23
Release:    0
Group:      Automotive/ICO Homescreen
License:    MIT
Source0:    %{name}-%{version}.tar.bz2

BuildRequires: pkgconfig(weston) >= 1.6.0
BuildRequires: pkgconfig(pixman-1)
BuildRequires: pkgconfig(evas)
BuildRequires: pkgconfig(aul)
BuildRequires: pkgconfig(ecore)
BuildRequires: pkgconfig(egl)
BuildRequires: pkgconfig
BuildRequires: mesa-devel
BuildRequires: genivi-shell-devel >= 0.2.4
BuildRequires: weston-ivi-shell-devel >= 0.1.8
Requires: weston >= 1.6.0
Requires: genivi-shell >= 0.2.4
Requires: weston-ivi-shell >= 0.1.8
Requires: weekeyboard
Conflicts: weston-ivi-config
Conflicts: weston-ivi-shell-config

%description
Weston Plugins for IVI package

%package devel
Summary:    Development files for %{name}
Group:      Graphics & UI Framework/Development
Requires:   %{name} = %{version}-%{release}

%description devel
Development files that expose the wayland extended protocols for IVI.

%prep
%setup -q -n %{name}-%{version}

%build
%reconfigure
%__make %{?_smp_mflags}

%install
%make_install

# configurations
mkdir -p %{buildroot}%{weston_conf} > /dev/null 2>&1
mkdir -p %{buildroot}%{_unitdir}/multi-user.target.wants > /dev/null 2>&1
install -m 0644 settings/weston.ini %{buildroot}%{weston_conf}
install -m 0644 settings/ico-pseudo-input-device.service %{buildroot}%{_unitdir}/ico-pseudo-input-device.service
ln -s %{_unitdir}/ico-pseudo-input-device.service %{buildroot}%{_unitdir}/multi-user.target.wants/ico-pseudo-input-device.service

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%manifest %{name}.manifest
%defattr(-,root,root,-)
%license COPYING
%dir %{_libdir}/weston/
%{_libdir}/weston/*.so
%{_libdir}/libico-uxf-weston-plugin.so.*
%{_bindir}/ico_send_inputevent
%{_bindir}/ico_pseudo_input_device
%{_unitdir}/ico-pseudo-input-device.service
%{_unitdir}/multi-user.target.wants/ico-pseudo-input-device.service
%config %{weston_conf}/weston.ini

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/%{name}/ico_input_mgr-client-protocol.h
%{_includedir}/%{name}/ico_window_mgr-client-protocol.h
%{_includedir}/%{name}/ico_input_mgr.h
%{_libdir}/libico-uxf-weston-plugin.so
