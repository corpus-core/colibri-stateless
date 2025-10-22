Name:           colibri-server
Version:        1.0.0
Release:        1%{?dist}
Summary:        Trustless stateless-client for Ethereum and L1/L2 networks

License:        MIT and PolyForm-Noncommercial-1.0.0
URL:            https://corpuscore.tech/
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.20
BuildRequires:  gcc
BuildRequires:  gcc-c++
BuildRequires:  openssl-devel
BuildRequires:  libcurl-devel
Requires:       openssl
Requires:       libcurl
Recommends:     memcached

%description
Colibri.stateless is a trustless stateless-client for Ethereum and other
L1 and L2 networks. It enables applications to access and verify blockchain
data independently by cryptographic proofs, without relying on trusted third
parties. Designed for IoT, mobile, and web environments, it uses zk-proofs
to deliver secure, efficient, and fully self-sovereign blockchain interaction.

This package installs the server and CLI tools:
 - colibri-server: Main prover/verifier server with HTTP API
 - colibri-prover: Generate cryptographic proofs
 - colibri-verifier: Verify cryptographic proofs
 - colibri-ssz: SSZ serialization utilities

Documentation: https://corpus-core.gitbook.io/specification-colibri-stateless
Copyright: corpus.core - https://corpuscore.tech/
Maintainer: Simon Jentzsch <simon@corpus.io>

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
    -DCLI=ON \
    -DTEST=OFF \
    -DINSTALLER=ON \
    ..
make -j4 colibri-server colibri-prover colibri-verifier colibri-ssz

%install
rm -rf %{buildroot}

# Install server binary
install -D -m 0755 build/bin/colibri-server %{buildroot}/usr/bin/colibri-server

# Install CLI tools
install -D -m 0755 build/bin/colibri-prover %{buildroot}/usr/bin/colibri-prover
install -D -m 0755 build/bin/colibri-verifier %{buildroot}/usr/bin/colibri-verifier
install -D -m 0755 build/bin/colibri-ssz %{buildroot}/usr/bin/colibri-ssz

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
/usr/bin/colibri-prover
/usr/bin/colibri-verifier
/usr/bin/colibri-ssz
%config(noreplace) /etc/colibri/server.conf
/usr/lib/systemd/system/colibri-server.service
%dir %attr(0755,colibri,colibri) /var/lib/colibri

%changelog
* Wed Oct 22 2025 Simon Jentzsch <simon@corpus.io> - 1.0.0-1
- Initial RPM release
- Ethereum mainnet and testnet support (Mainnet, Sepolia, Holesky)
- Light client proof generation and verification
- BLS signature verification
- Memcached integration for caching
- systemd service integration
- CLI tools included (colibri-prover, colibri-verifier, colibri-ssz)
- Web UI for configuration (optional, disabled by default)
- Support for IoT, mobile, and web environments

