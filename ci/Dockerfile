FROM centos:7

# Install PostgreSQl Repo RPM
RUN curl -O https://download.postgresql.org/pub/repos/yum/reporpms/EL-7-x86_64/pgdg-redhat-repo-latest.noarch.rpm

RUN yum install -y epel-release ./pgdg*.rpm

RUN yum install -y \
        sudo \
        which \
        rpm-build \
        libtool \
        postgresql94 \
        postgresql94-devel \
        glib2-devel \
        python-devel \
        jansson-devel \
        libini_config-devel \
        openssl-devel \
        libattr-devel \
        sg3_utils-devel \
        postgresql94-server \
        postgresql94-contrib \
        glib2 \
        jansson \
        libini_config \
        openssl \
        libattr \
        python \
        python-argparse \
        python-yaml \
        clustershell \
        python-psycopg2 \
        attr \
        mtx

# TODO: lin_tape ltfssde and QuadstorVTL setup

RUN sudo -u postgres /usr/pgsql-9.4/bin/initdb -D /tmp/pg_data  \
                     --auth-local trust

ENV PKG_CONFIG_PATH /usr/pgsql-9.4/lib/pkgconfig/

WORKDIR /phobos

COPY . .

# FIXME: postgresql is started in this container because most tests have
# hardcoded localhost postgresql connection information
CMD ["/bin/sh", "-c", "START_PGSQL=true ./ci/run-ci.sh"]
