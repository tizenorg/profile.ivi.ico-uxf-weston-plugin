Name:       ico-uxf-weston-plugin
Summary:    Weston Plugins for IVI
Version:    0.9.22
Release:    1.1
Group:      Graphics & UI Framework/Automotive UI
License:    MIT
URL:        ""
Source0:    %{name}-%{version}.tar.bz2

BuildRequires: pkgconfig(weston) >= 1.4.0
BuildRequires: pkgconfig(pixman-1)
BuildRequires: pkgconfig(evas)
BuildRequires: pkgconfig(aul)
BuildRequires: pkgconfig(ecore)
BuildRequires: mesa-devel
BuildRequires: genivi-shell-devel
BuildRequires: weston-ivi-shell-devel
Requires: weston >= 1.4.0
Requires: genivi-shell
Requires: weston-ivi-shell
Requires: weekeyboard
Conflicts: weston-ivi-config
Conflicts: weston-ivi-shell-config

%description
Weston Plugins for IVI

%package devel
Summary:    Development files for %{name}
Group:      Graphics & UI Framework/Development
Requires:   %{name} = %{version}-%{release}

%description devel
Development files that expose the wayland extended protocols for IVI.

%prep
%setup -q -n %{name}-%{version}

%build
%autogen

%configure
make %{?_smp_mflags}

%install
%make_install

# configurations
%define weston_conf %{_sysconfdir}/xdg/weston
%define systemddir /usr/lib/systemd
mkdir -p %{buildroot}%{weston_conf} > /dev/null 2>&1
mkdir -p %{buildroot}%{systemddir}/system/multi-user.target.wants > /dev/null 2>&1
install -m 0644 settings/weston.ini %{buildroot}%{weston_conf}
install -m 0644 settings/ico-pseudo-input-device.service %{buildroot}%{systemddir}/system/ico-pseudo-input-device.service
ln -s %{systemddir}/system/ico-pseudo-input-device.service %{buildroot}%{systemddir}/system/multi-user.target.wants/ico-pseudo-input-device.service

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
%{systemddir}/system/ico-pseudo-input-device.service
%{systemddir}/system/multi-user.target.wants/ico-pseudo-input-device.service
%{weston_conf}/weston.ini

%files devel
%manifest %{name}.manifest
%defattr(-,root,root,-)
%{_includedir}/%{name}/ico_input_mgr-client-protocol.h
%{_includedir}/%{name}/ico_window_mgr-client-protocol.h
%{_includedir}/%{name}/ico_input_mgr.h
%{_libdir}/libico-uxf-weston-plugin.so
