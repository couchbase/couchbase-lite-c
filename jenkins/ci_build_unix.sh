#!/bin/bash -ex

# NOTE: This is for Couchbase internal CI usage.  
# This room is full of dragons, so you *will* get confused.  
# You have been warned.

# Global define
PRODUCT=${1}
BLD_NUM=${2}
VERSION=${3}
EDITION=${4}

if [[ -z "${WORKSPACE}" ]]; then
    WORKSPACE=`pwd`
fi

mkdir -p ${WORKSPACE}/build_release

case "${OSTYPE}" in
    darwin*)  OS="macos"
              PKG_CMD='zip -r --symlinks'
              PKG_TYPE='zip'
              PROP_FILE=${WORKSPACE}/publish.prop
              ;;
    linux*)   PKG_CMD='tar czf'
              PKG_TYPE='tar.gz'
              PROP_FILE=${WORKSPACE}/publish.prop
              OS_NAME=`lsb_release -is`
              OS_VERSION=`lsb_release -rs`
              OS_ARCH=`uname -m`
              OS=${OS_NAME,,}${OS_VERSION}-${OS_ARCH}
              ;;
    *)        echo "unknown: $OSTYPE"
              exit 1;;
esac

project_dir=couchbase-lite-c
strip_dir=${project_dir}
macos_lib="libcblite.dylib"

echo VERSION=${VERSION}
# Global define end

ln -sf ${WORKSPACE}/couchbase-lite-c-ee/couchbase-lite-core-EE ${WORKSPACE}/couchbase-lite-c/vendor/couchbase-lite-core-EE

if [[ -z ${SKIP_TESTS} ]] && [[ ${EDITION} == 'enterprise'* ]]; then
    echo "==== Download Vector Search Extension for Tests  ==="
    pushd ${project_dir}
    if [[ $OSTYPE == 'darwin'* ]]; then
    ./scripts/download_vector_search_extension.sh apple
    else
    ./scripts/download_vector_search_extension.sh linux
    fi
    popd
fi

echo "====  Building macos/linux Release binary  ==="
cd ${WORKSPACE}/build_release
cmake -DEDITION=${EDITION} -DCMAKE_INSTALL_PREFIX=`pwd`/libcblite-$VERSION -DCMAKE_BUILD_TYPE=MinSizeRel ..
make -j8
if [[ "${OS}" == "macos" ]]; then
    pushd ${project_dir}
    dsymutil ${macos_lib} -o libcblite.dylib.dSYM
    strip -x ${macos_lib}
    popd
else
    ${WORKSPACE}/couchbase-lite-c/jenkins/strip.sh ${strip_dir}
fi

make install
if [[ "${OS}" == "macos" ]]; then
    # package up the strip symbols
    cp -rp ${strip_dir}/libcblite.dylib.dSYM  ./libcblite-$VERSION/
else
    # package up the strip symbols
    cp -rp ${strip_dir}/libcblite.so.sym  ./libcblite-$VERSION/
fi

if [[ -z ${SKIP_TESTS} ]] && [[ ${EDITION} == 'enterprise' ]]; then
    cd ${WORKSPACE}/build_release/${project_dir}/test
    ./CBL_C_Tests -r quiet || exit 1
fi

cd ${WORKSPACE}

PACKAGE_NAME=${PRODUCT}-${EDITION}-${VERSION}-${BLD_NUM}-${OS}.${PKG_TYPE}
echo
echo  "=== Creating ${WORKSPACE}/${PACKAGE_NAME} package ==="
echo

cd ${WORKSPACE}/build_release/
cp ${WORKSPACE}/product-texts/mobile/couchbase-lite/license/LICENSE_$EDITION.txt libcblite-$VERSION/LICENSE.txt
#notices.txt is produced by blackduck.
#It is not part of source tar, it is download to the workspace by a separate curl command by jenkins job.
if [[ -f ${WORKSPACE}/notices.txt ]]; then
    cp ${WORKSPACE}/notices.txt libcblite-$VERSION/notices.txt
fi
# Create separate symbols pkg
if [[ "${OS}" == "macos" ]]; then
    ${PKG_CMD} ${WORKSPACE}/${PACKAGE_NAME} libcblite-$VERSION/*.txt libcblite-$VERSION/include libcblite-$VERSION/lib
    SYMBOLS_RELEASE_PKG_NAME=${PRODUCT}-${EDITION}-${VERSION}-${BLD_NUM}-${OS}-'symbols'.${PKG_TYPE}
    ${PKG_CMD} ${WORKSPACE}/${SYMBOLS_RELEASE_PKG_NAME}  libcblite-$VERSION/libcblite.dylib.dSYM
else # linux
    ${PKG_CMD} ${WORKSPACE}/${PACKAGE_NAME} libcblite-$VERSION/*.txt libcblite-$VERSION/include libcblite-$VERSION/lib
    SYMBOLS_RELEASE_PKG_NAME=${PRODUCT}-${EDITION}-${VERSION}-${BLD_NUM}-${OS}-'symbols'.${PKG_TYPE}
    ${PKG_CMD} ${WORKSPACE}/${SYMBOLS_RELEASE_PKG_NAME} libcblite-$VERSION/libcblite*.sym
fi

cd ${WORKSPACE}

echo "PRODUCT=${PRODUCT}"  >> ${PROP_FILE}
echo "BLD_NUM=${BLD_NUM}"  >> ${PROP_FILE}
echo "VERSION=${VERSION}" >> ${PROP_FILE}
echo "PKG_TYPE=${PKG_TYPE}" >> ${PROP_FILE}
echo "RELEASE_PKG_NAME=${PACKAGE_NAME}" >> ${PROP_FILE}
echo "SYMBOLS_RELEASE_PKG_NAME=${SYMBOLS_RELEASE_PKG_NAME}" >> ${PROP_FILE}

echo
echo  "=== Created ${WORKSPACE}/${PROP_FILE} ==="
echo

cat ${PROP_FILE}
