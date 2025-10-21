Name:           colibri-server
Version:        1.0.0
Release:        1%{?dist}
Summary:        Colibri Stateless Prover Server

License:        MIT and PolyForm-Noncommercial-1.0.0
URL:            https://github.com/corpus-core/colibri-stateless
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.10
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  openssl-devel
Requires:       openssl
Recommends:     memcached

%description
Colibri Stateless is a highly efficient prover/verifier for Ethereum
and Layer-2 solutions. This package provides the prover server that
can create cryptographic proofs for blockchain data.

The server includes:
 - Ethereum mainnet and testnet support
 - Light client proof generation
 - BLS signature verification
 - Memcached integration for caching
 - RESTful API for proof requests

%prep
%setup -q

%build
mkdir -p build
cd build
cmake -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DHTTP_SERVER=ON \
    -DPROVER=ON \
    -DVERIFIER=ON \
    -DPROVER_CACHE=ON \
    -DCLI=OFF \
    -DTEST=OFF \
    -DINSTALLER=ON \
    ..
make -j4 server

%install
rm -rf %{buildroot}

# Install binary
install -D -m 0755 build/default/bin/server %{buildroot}/usr/bin/colibri-server

# Install config
install -D -m 0644 installer/config/server.conf.default %{buildroot}/etc/colibri/server.conf

# Install systemd service
install -D -m 0644 installer/scripts/systemd/colibri-server.service \
    %{buildroot}/usr/lib/systemd/system/colibri-server.service

# Create working directory
mkdir -p %{buildroot}/var/lib/colibri

%pre
# Create user if it doesn't exist
getent group colibri >/dev/null || groupadd -r colibri
getent passwd colibri >/dev/null || \
    useradd -r -g colibri -d /var/lib/colibri -s /sbin/nologin \
    -c "Colibri Server" colibri
exit 0

%post
# Set permissions
chown -R root:colibri /etc/colibri
chmod 750 /etc/colibri
chmod 640 /etc/colibri/server.conf

chown colibri:colibri /var/lib/colibri
chmod 755 /var/lib/colibri

# Reload systemd and enable service
%systemd_post colibri-server.service

echo ""
echo "====================================================================="
echo "Colibri Server has been installed successfully!"
echo ""
echo "Configuration file: /etc/colibri/server.conf"
echo "Edit this file to customize your server settings."
echo ""
echo "To start the service:   systemctl start colibri-server"
echo "To stop the service:    systemctl stop colibri-server"
echo "To view logs:           journalctl -u colibri-server -f"
echo "====================================================================="
echo ""

%preun
%systemd_preun colibri-server.service

%postun
%systemd_postun_with_restart colibri-server.service

# Only remove user on uninstall (not upgrade)
if [ $1 -eq 0 ]; then
    echo "Note: Configuration and data in /var/lib/colibri and /etc/colibri"
    echo "      have been preserved. Remove manually if desired."
fi

%files
%defattr(-,root,root,-)
/usr/bin/colibri-server
%config(noreplace) /etc/colibri/server.conf
/usr/lib/systemd/system/colibri-server.service
%dir %attr(0755,colibri,colibri) /var/lib/colibri

%changelog
* Tue Oct 21 2025 Corpus Core <simon@corpus.io> - 1.0.0-1
- Initial RPM release
- Ethereum mainnet and testnet support
- Beacon chain proof verification
- Memcached integration
- systemd service integration

