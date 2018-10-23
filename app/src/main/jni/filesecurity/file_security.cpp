//
// Created by famgy on 18-9-14.
//

#include <jni.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <android/log.h>
#include <string.h>
#include <malloc.h>
#include <iostream>
#include <set>
#include <sys/syscall.h>
#include <map>
#include <errno.h>
#include <unistd.h>
#include <regex.h>

#include "../hook/inlinehook/inlineHook.h"

#include "file_security.h"
#include "file_encrypt.h"

#define _LARGEFILE64_SOURCE


extern "C" int __openat(int, const char*, int, int);

extern const char* inline_originApk;
extern const char* inline_baseApk;
extern int inline_baseApkL;

std::map<int, FILE_FD_INFO_S *> g_FileFdMap;

#define SUNINFO 7
#define BLOCK_SIZE 512

int (*old_openat)(int, const char *, int, int) = NULL;
int (*old_stat)(const char *pathname, struct stat *statbuf) = NULL;
int (*old_fstat)(int fd, struct stat *statbuf) = NULL;

ssize_t (*old_pread)(int fd, void *buf, size_t count, off_t offset) = NULL;
ssize_t (*old_pwrite)(int fd, const void *buf, size_t count, off_t offset) = NULL;
ssize_t (*old_read)(int fd, void *buf, size_t count) = NULL;
ssize_t (*old_write)(int fd, const void *buf, size_t count) = NULL;
off_t (*old_lseek)(int fd, off_t offset, int whence) = NULL;
off64_t (*old_lseek64)(int fd, off64_t offset, int whence) = NULL;

int (*old_close)(int fd) = NULL;


void testBreak(int va_whence, int tmp_whence) {
/*    int a = 1;

    char *buffer = NULL;

    buffer[0] = 'A';*/

    __android_log_print(ANDROID_LOG_DEBUG, "\n\ninlinehook=====", "testBreak addr=%p, va_whence = %d, tmp_whence = %d\n\n", testBreak, va_whence, tmp_whence);


    return;
}

static int fileEncrypt(int dirFd, const char *srcPathName)
{
    int fd_s = old_openat(dirFd, srcPathName, O_RDWR, 0640);
    if (fd_s == -1) {
        __android_log_print(ANDROID_LOG_DEBUG, "inline======", "open failed : %s", strerror(errno));
        return fd_s;
    }

    char pathNameTmp[2048] = {0};
    strcat(pathNameTmp, srcPathName);
    strcat(pathNameTmp, "_tmp");
    int fd_d = old_openat(dirFd, pathNameTmp, O_CREAT | O_RDWR, 0640);
    if (fd_d == -1) {
        __android_log_print(ANDROID_LOG_DEBUG, "inline======", "open failed : %s", strerror(errno));
        old_close(fd_d);
        return fd_d;
    }

    //Write encrypt head
    old_write(fd_d, "suninfo", SUNINFO);

    //Write encrypt body
    unsigned char plaintBuffer[BLOCK_SIZE]={0};
    unsigned char cipherBuffer[BLOCK_SIZE]={0};
    int readBufferLen;
    int ciphierBufferSize;

    while ((readBufferLen = old_read(fd_s, plaintBuffer, BLOCK_SIZE)) > 0) {
        if (readBufferLen == BLOCK_SIZE) {
            ciphierBufferSize = fileSm4Encrypt(plaintBuffer, readBufferLen, cipherBuffer);
        } else if (readBufferLen < BLOCK_SIZE) {
            ciphierBufferSize = fileXorEncrypt(plaintBuffer, readBufferLen, cipherBuffer);
        }
        write(fd_d, cipherBuffer, ciphierBufferSize);
    }

    old_close(fd_s);
    old_close(fd_d);

    // Mv file
    unlink(srcPathName);
    rename(pathNameTmp, srcPathName);

    return 0;
}

static FILE_FD_INFO_S *findFileFdInfo(const int fd) {
    std::map<int, FILE_FD_INFO_S *>::iterator it;
    FILE_FD_INFO_S *pstFileFdInfo = NULL;

    it = g_FileFdMap.find(fd);
    if (it != g_FileFdMap.end()) {
        pstFileFdInfo = it->second;
    }

    return pstFileFdInfo;
}

static bool regMatchFilePath(const char *pathName)
{
    bool result = false;
    regex_t reg;
    regmatch_t pm[1];
    int  status = 0;
    //char pattern[] = "/tencent/QQfile_recv/";
    //char pattern[] = "/storage/emulated/0/Netease/Mail/0/.attachments/docx_test.docx";

    char pattern[] = "(/tencent/QQmail/tmp/|/tencent/QQfile_recv/|/Netease/Mail/0/.attachments/)";

    /*正则表达式：编译-匹配-释放*/
    status = regcomp(&reg, pattern, REG_EXTENDED|REG_NEWLINE);  //扩展正则表达式和识别换行符
    if (status != 0){    //成功返回0
        return false;
    }

    status = regexec(&reg, pathName, 1, pm, 0);
    if (status == 0){
        __android_log_print(ANDROID_LOG_DEBUG, "inline======", "regMatchFilePath matched");
        result = true;
    }

    regfree(&reg);

    return result;
}

static bool regMatchFileType(const char *pathName)
{
    bool result = false;
    regex_t reg;
    regmatch_t pm[1];
    int  status = 0;
    //char pattern[] = "[.](txt|pdf|docx|pptx|xlsx)$";
    char pattern[] = "(txt_test.txt|pdf_test.pdf|docx_test.docx|pptx_test.pptx|xlsx_test.xlsx)";

    /*正则表达式：编译-匹配-释放*/
    status = regcomp(&reg, pattern, REG_EXTENDED|REG_NEWLINE);  //扩展正则表达式和识别换行符
    if (status != 0){    //成功返回0
        return false;
    }

    status = regexec(&reg, pathName, 1, pm, 0);
    if (status == 0){
        __android_log_print(ANDROID_LOG_DEBUG, "inline======", "regMatchFileType matched\n");
        result = true;
    }

    regfree(&reg);

    return result;
}

static int __nativehook_impl_android_openat(int dirFd, const char *pathName, int flag, int mode) {

    // 破解防打包
//    int lo = strlen(pathName);
//    if (lo == inline_baseApkL && strncmp(inline_baseApk, pathName, lo) == 0) {
//        //__android_log_print(ANDROID_LOG_DEBUG, "xhook", "open : %s replace %s\n", inline_originApk, pathname);
//        return old_openat(dirFd, inline_originApk, flag, mode);
//    }

    // File Security
    if (regMatchFilePath(pathName) == false && regMatchFileType(pathName) == false) {

        int fd = old_openat(dirFd, pathName, flag, mode);
        //__android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "regMatch failed, openat:%s, fd = %d\n", pathName, fd);
        return fd;
    }

    if (flag & O_APPEND) {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "openat:%s, flagV = %s\n", pathName, "O_APPEND");
    }

    if (flag & O_TRUNC) {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "openat:%s, flagV = %s\n", pathName, "O_TRUNC");
    }

    if (flag & O_NOATIME) {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "openat:%s, flagV = %s\n", pathName, "O_NOATIME");
    }

    if (access(pathName, F_OK) == 0) {
        int fd_o = old_openat(dirFd, pathName, O_RDWR, 0640);
        if (fd_o == -1) {
            return -1;
        }
        char suninfoBuffer[SUNINFO] = {0};
        old_read(fd_o, suninfoBuffer, SUNINFO);
        old_close(fd_o);

        if (memcmp(suninfoBuffer, "suninfo", SUNINFO) == 0) {
            int fdTmp = old_openat(dirFd, pathName, flag, mode);
            if (fdTmp != -1) {
                // Add file-fd-list for matching.
                FILE_FD_INFO_S *pstFileFdInfo = (FILE_FD_INFO_S *) malloc(sizeof(FILE_FD_INFO_S));
                if (pstFileFdInfo == NULL) {
                    return -1;
                }

                pstFileFdInfo->fd = fdTmp;
                pstFileFdInfo->dirFd = dirFd;
                pstFileFdInfo->flag = flag;
                strcpy(pstFileFdInfo->szFilePath, pathName);
                g_FileFdMap.insert(std::pair<int, FILE_FD_INFO_S *>(fdTmp, pstFileFdInfo));

                old_lseek(fdTmp, SUNINFO, SEEK_SET);
                __android_log_print(ANDROID_LOG_DEBUG, "inlinehook==hasSuninfo=====",
                                    "init , open: fdTmp = %d, curOffset = %d\n", fdTmp,
                                    (int) old_lseek(fdTmp, 0, SEEK_CUR));
            }

            return fdTmp;
        }

        //File encrypt
        if (fileEncrypt(dirFd, pathName) != 0) {
            __android_log_print(ANDROID_LOG_DEBUG, "\n\ninlinehook=====", "file : %s, encrypt failed\n\n", pathName);
        }
    }

    int fd = old_openat(dirFd, pathName, flag, mode);
    __android_log_print(ANDROID_LOG_DEBUG, "inlinehook====", "openat:%s, fd = %d\n", pathName, fd);

    //Save file info
    if (fd != -1) {
        // Add file-fd-list for matching.
        FILE_FD_INFO_S *pstFileFdInfo = (FILE_FD_INFO_S *) malloc(sizeof(FILE_FD_INFO_S));
        if (pstFileFdInfo == NULL) {
            return -1;
        }

        pstFileFdInfo->fd = fd;
        pstFileFdInfo->flag = flag;
        g_FileFdMap.insert(std::pair<int, FILE_FD_INFO_S *>(fd, pstFileFdInfo));

        old_lseek(fd, SUNINFO, SEEK_SET);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook=====", "end , open: fd = %d, curOffset = %d\n", fd, (int) old_lseek(fd, 0, SEEK_CUR));
    }

    return fd;
}

static int __nativehook_impl_android_close(int fd) {
    std::map<int, FILE_FD_INFO_S *>::iterator it;
    int result = 0;

    it = g_FileFdMap.find(fd);
    if (it != g_FileFdMap.end()) {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "close: fd = %d", fd);

        result = old_close(fd);
        if (result == 0) {
            g_FileFdMap.erase(it);
            FILE_FD_INFO_S *fileFdInfoTmp = it->second;
            free(fileFdInfoTmp);

            __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "finish close: fd = %d", fd);
        }
    } else {
//        result = syscall(SYS_close, fd);
        result = old_close(fd);
    }

    return result;
}

static size_t findDecryptPoint(off_t curOffset, size_t *DecryptPoint) {
    int i;
    size_t relativeOffset;
    size_t descryptOffset = curOffset - SUNINFO;

    for (i = 0;;i = i + BLOCK_SIZE) {
        if (i + BLOCK_SIZE > descryptOffset) {
            break;
        }
    }

    *DecryptPoint = i + SUNINFO;
    __android_log_print(ANDROID_LOG_DEBUG, "after==inlinehook=====", "DecryptPoint = %ld", *DecryptPoint);
    relativeOffset = descryptOffset - i;

    return relativeOffset;
}

static int bufferEncrypt(int fd, const void *buf,size_t count, off_t offset)
{
    unsigned char *inBuf = (unsigned char *)buf;
    size_t relativeOffset = 0;
    size_t decryptPoint = 0;
    unsigned char plaintBuffer[BLOCK_SIZE] = {0};
    unsigned char cipherBuffer[BLOCK_SIZE] = {0};
    int plaintBufferSize = 0;
    int cipherBufferSize = 0;
    size_t fdCurOffset = 0;
    size_t fdCurWOffset = 0;


    if (offset != -1) {
        fdCurOffset = old_lseek(fd, offset, SEEK_SET);
    } else {
        fdCurOffset = old_lseek(fd, 0, SEEK_CUR);
    }

    std::map<int, FILE_FD_INFO_S *>::iterator it;
    it = g_FileFdMap.find(fd);
    FILE_FD_INFO_S *fileFdInfoTmp = it->second;
    int fd_r = old_openat(fileFdInfoTmp->dirFd, fileFdInfoTmp->szFilePath, O_RDWR, 0640);
    if (fd == -1) {
        return -1;
    }

    // set offset decryptPoint
    relativeOffset = findDecryptPoint(fdCurOffset, &decryptPoint);
    fdCurWOffset = old_lseek(fd_r, decryptPoint, SEEK_SET);

    int ret;
    int readBufferLen;
    size_t hasEffectSize = 0;
    while (hasEffectSize < count) {
        readBufferLen = old_read(fd_r, cipherBuffer, BLOCK_SIZE);
        if (readBufferLen == -1) {
            return -1;
        } else if (readBufferLen < relativeOffset) {
            return 0;
        }

        if (readBufferLen == BLOCK_SIZE) {
            plaintBufferSize = fileSm4Decrypt(cipherBuffer, BLOCK_SIZE, plaintBuffer);
            if (relativeOffset != 0) {
                if (hasEffectSize + (plaintBufferSize - relativeOffset) < count) {
                    memcpy(plaintBuffer + relativeOffset, inBuf + hasEffectSize, plaintBufferSize - relativeOffset);
                    hasEffectSize += plaintBufferSize - relativeOffset;

                    cipherBufferSize = fileSm4Encrypt(plaintBuffer, BLOCK_SIZE, cipherBuffer);
                    ret = old_pwrite(fd, cipherBuffer, cipherBufferSize, fdCurWOffset);
                    if (ret == -1) {
                        __android_log_print(ANDROID_LOG_DEBUG, "inline======", "open failed : %s", strerror(errno));
                        return -1;
                    }
                    fdCurWOffset += cipherBufferSize;
                } else if (hasEffectSize + (plaintBufferSize - relativeOffset) == count) {
                    memcpy(plaintBuffer + relativeOffset, inBuf + hasEffectSize, plaintBufferSize - relativeOffset);
                    hasEffectSize += plaintBufferSize - relativeOffset;

                    cipherBufferSize = fileSm4Encrypt(plaintBuffer, BLOCK_SIZE, cipherBuffer);
                    ret = old_pwrite(fd, cipherBuffer, cipherBufferSize, fdCurWOffset);
                    if (ret == -1) {
                        __android_log_print(ANDROID_LOG_DEBUG, "inline======", "open failed : %s", strerror(errno));
                        return -1;
                    }
                    fdCurWOffset += cipherBufferSize;
                    break;
                } else {
                    memcpy(plaintBuffer + relativeOffset, inBuf + hasEffectSize, count - hasEffectSize);
                    hasEffectSize += count - hasEffectSize;

                    cipherBufferSize = fileSm4Encrypt(plaintBuffer, BLOCK_SIZE, cipherBuffer);
                    ret = old_pwrite(fd, cipherBuffer, cipherBufferSize, fdCurWOffset);
                    if (ret == -1) {
                        __android_log_print(ANDROID_LOG_DEBUG, "inline======", "open failed : %s", strerror(errno));
                        return -1;
                    }
                    fdCurWOffset += cipherBufferSize;
                    break;
                }
                relativeOffset = 0;
            } else {
                if (hasEffectSize + plaintBufferSize < count) {
                    cipherBufferSize = fileSm4Encrypt(inBuf + hasEffectSize, BLOCK_SIZE, cipherBuffer);
                    hasEffectSize =+ cipherBufferSize;

                    old_pwrite(fd, cipherBuffer, cipherBufferSize, fdCurWOffset);
                    fdCurWOffset += cipherBufferSize;
                } else if (hasEffectSize + plaintBufferSize == count) {
                    cipherBufferSize = fileSm4Encrypt(inBuf + hasEffectSize, BLOCK_SIZE, cipherBuffer);
                    hasEffectSize =+ cipherBufferSize;

                    old_pwrite(fd, cipherBuffer, cipherBufferSize, fdCurWOffset);
                    fdCurWOffset += cipherBufferSize;
                    break;
                } else {
                    memcpy(plaintBuffer, inBuf + hasEffectSize, count - hasEffectSize);
                    hasEffectSize += count - hasEffectSize;

                    cipherBufferSize = fileSm4Encrypt(plaintBuffer, BLOCK_SIZE, cipherBuffer);
                    old_pwrite(fd, cipherBuffer, cipherBufferSize, fdCurWOffset);
                    fdCurWOffset += cipherBufferSize;
                    break;
                }
            }
        } else if (readBufferLen < BLOCK_SIZE) {
            break;
        }
    }

    while (hasEffectSize < count) {
        if (hasEffectSize + BLOCK_SIZE < count) {
            cipherBufferSize = fileSm4Encrypt(inBuf + hasEffectSize, BLOCK_SIZE, cipherBuffer);
            hasEffectSize += cipherBufferSize;

            old_pwrite(fd, cipherBuffer, cipherBufferSize, fdCurWOffset);
            fdCurWOffset += cipherBufferSize;
        } else if (hasEffectSize + BLOCK_SIZE == count){
            cipherBufferSize = fileSm4Encrypt(inBuf + hasEffectSize, BLOCK_SIZE, cipherBuffer);
            hasEffectSize += cipherBufferSize;

            old_pwrite(fd, cipherBuffer, cipherBufferSize, fdCurWOffset);
            fdCurWOffset += cipherBufferSize;

            break;
        } else {
            cipherBufferSize = fileXorEncrypt(inBuf + hasEffectSize, count - hasEffectSize, cipherBuffer);
            hasEffectSize += count - hasEffectSize;

            old_pwrite(fd, cipherBuffer, cipherBufferSize, fdCurWOffset);
            fdCurWOffset += count - hasEffectSize;

            break;
        }
    }

    old_close(fd_r);

    // recover offset
    old_lseek(fd, fdCurOffset + hasEffectSize, SEEK_SET);

    return hasEffectSize;
}

static int bufferDecrypt(int fd, void *buf, size_t count, off_t offset)
{
    char *outBuf = (char *)buf;
    size_t relativeOffset;
    size_t decryptPoint;
    unsigned char plaintBuffer[BLOCK_SIZE];
    unsigned char cipherBuffer[BLOCK_SIZE];
    int plaintBufferSize;
    int readBufferLen;
    size_t fdCurOffset;

    if (offset != -1) {
        fdCurOffset = old_lseek(fd, offset, SEEK_SET);
    } else {
        fdCurOffset = old_lseek(fd, 0, SEEK_CUR);
    }

    // set offset decryptPoint
    relativeOffset = findDecryptPoint(fdCurOffset, &decryptPoint);
    old_lseek(fd, decryptPoint, SEEK_SET);

    size_t hasEffectSize = 0;
    while (hasEffectSize < count) {
        readBufferLen = old_read(fd, cipherBuffer, BLOCK_SIZE);
        if (readBufferLen == -1) {
            return -1;
        } else if (readBufferLen == 0) {
            break;
        } else if (readBufferLen <= relativeOffset) {
            return 0;
        }

        if (readBufferLen == BLOCK_SIZE){
            plaintBufferSize = fileSm4Decrypt(cipherBuffer, readBufferLen, plaintBuffer);
            if (relativeOffset != 0) {
                if (hasEffectSize + (plaintBufferSize - relativeOffset) < count) {
                    memcpy(outBuf + hasEffectSize, plaintBuffer + relativeOffset, plaintBufferSize - relativeOffset);
                    hasEffectSize += plaintBufferSize - relativeOffset;
                } else if (hasEffectSize + (plaintBufferSize - relativeOffset) == count) {
                    memcpy(outBuf + hasEffectSize, plaintBuffer + relativeOffset, plaintBufferSize - relativeOffset);
                    hasEffectSize += plaintBufferSize - relativeOffset;
                    break;
                } else {
                    memcpy(outBuf + hasEffectSize, plaintBuffer + relativeOffset, count - hasEffectSize);
                    hasEffectSize += count - hasEffectSize;
                    break;
                }
                relativeOffset = 0;
            } else {
                if (hasEffectSize + plaintBufferSize < count) {
                    memcpy(outBuf + hasEffectSize, plaintBuffer, plaintBufferSize);
                    hasEffectSize += plaintBufferSize;
                } else if (hasEffectSize + plaintBufferSize == count) {
                    memcpy(outBuf + hasEffectSize, plaintBuffer, plaintBufferSize);
                    hasEffectSize += plaintBufferSize;
                    break;
                } else {
                    memcpy(outBuf + hasEffectSize, plaintBuffer, count - hasEffectSize);
                    hasEffectSize += count - hasEffectSize;
                    break;
                }
            }
        } else if (readBufferLen < BLOCK_SIZE) {
            plaintBufferSize = fileXorDecrypt(cipherBuffer, readBufferLen, plaintBuffer);
            if (hasEffectSize + plaintBufferSize <= count) {
                memcpy(outBuf + hasEffectSize, plaintBuffer, plaintBufferSize);
                hasEffectSize += plaintBufferSize;
            } else {
                memcpy(outBuf + hasEffectSize, plaintBuffer, count - hasEffectSize);
                hasEffectSize += count - hasEffectSize;
            }

            break;
        }
    }

    // recover offset
    old_lseek(fd, fdCurOffset + hasEffectSize, SEEK_SET);

    return hasEffectSize;
}

static ssize_t __nativehook_impl_android_read(int fd, void *buf, size_t count) {
    //__android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "pread: fd = %d, count = %d, offset = %d\n", count);

    ssize_t r_len;
    size_t curOffset = old_lseek(fd, 0, SEEK_CUR);

    if (NULL != findFileFdInfo(fd)) {
        __android_log_print(ANDROID_LOG_DEBUG, "\ninlinehook=====", "init , read: fd = %d, count = %d, curOffset = %d", fd, count, curOffset);

        r_len = bufferDecrypt(fd, buf, count, -1);

        if (r_len > 0) {
            __android_log_print(ANDROID_LOG_DEBUG, "after==inlinehook=====", "read: buffer = %x, %x",
                                ((char *) buf)[0], ((char *) buf)[1]);
        }
    } else {
        r_len = old_read(fd, buf, count);
    }

    return r_len;
}

static ssize_t __nativehook_impl_android_pread(int fd, void *buf, size_t count, off_t offset) {
    //__android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "pread: fd = %d, count = %d, offset = %d\n", fd, count, offset);

    ssize_t r_len;

    if (NULL != findFileFdInfo(fd)) {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook=====", "pread: fd = %d, count = %d, offset = %ld\n", fd, count, offset);

        r_len = bufferDecrypt(fd, buf, count, offset + SUNINFO);
        if (r_len > 0) {
            __android_log_print(ANDROID_LOG_DEBUG, "after==inlinehook=====", "pread: buffer = %x, %x\n",
                                ((char *) buf)[0], ((char *) buf)[1]);
        } else if (r_len > count) {
            size_t curOffset = (int) old_lseek(fd, 0, SEEK_CUR);
            __android_log_print(ANDROID_LOG_DEBUG, "===finish==inlinehook=====", "Failed !!!! : curOffset = %ld, returnLen = %ld\n\n", curOffset, r_len);
            return -1;
        }
    } else {
        r_len = old_pread(fd, buf, count, offset);
    }

    return r_len;
}

static ssize_t __nativehook_impl_android_write(int fd, const void *buf, size_t count) {
    ssize_t r_len;
    FILE_FD_INFO_S *pstFileFdInfo = findFileFdInfo(fd);

    if (NULL != pstFileFdInfo) {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook=====", "write: filepath = %s, fd = %d, count = %d",
                            pstFileFdInfo->szFilePath, fd, count);

        if ((pstFileFdInfo->flag & O_TRUNC != 0) && (old_lseek(fd, 0, SEEK_CUR) == SUNINFO)) {
            old_lseek(fd, 0, SEEK_SET);
            old_write(fd, "suninfo", SUNINFO);
        }

        r_len = bufferEncrypt(fd, buf, count, -1);

        return r_len;
    }

    return old_write(fd, buf, count);
}

static ssize_t __nativehook_impl_android_pwrite(int fd, const void *buf, size_t count, off_t offset) {
    ssize_t r_len;

    FILE_FD_INFO_S *pstFileFdInfo = findFileFdInfo(fd);

    if (NULL != pstFileFdInfo) {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook=====", "write: filepath = %s, fd = %d, count = %d",
                            pstFileFdInfo->szFilePath, fd, count);

        if ((pstFileFdInfo->flag & O_TRUNC != 0) && (old_lseek(fd, 0, SEEK_CUR) == SUNINFO)) {
            old_lseek(fd, 0, SEEK_SET);
            old_write(fd, "suninfo", SUNINFO);
        }

        r_len = bufferEncrypt(fd, buf, count, offset + SUNINFO);

        return r_len;
    }

    return old_pwrite(fd, buf, count, offset);
}

static off_t __nativehook_impl_android_lseek(int fd, off_t offset, int whence) {
    FILE_FD_INFO_S *pstFileFdInfo = findFileFdInfo(fd);
    off_t var_off_t = 0;

    if (NULL != pstFileFdInfo) {
        __android_log_print(ANDROID_LOG_DEBUG, "\n\ninlinehook=====",
                            "lseek: fd = %d, offset = %d, whence = %d\n", fd, offset, whence);

        // Show stack functions
//        if (10717 == offset) {
////            JNIEnv *env;
////
////            if (inline_android_vm->GetEnv((void **) &env, JNI_VERSION_1_4) == JNI_OK) {
////                env->FindClass(NULL);
////            }
//
//            testBreak(0, 0);
//        }


        if (whence == SEEK_CUR) {
            var_off_t = old_lseek(fd, 0, SEEK_CUR);
            __android_log_print(ANDROID_LOG_DEBUG, "\n\ninlinehook=====00",
                                "lseek(SEEK_CUR): fd = %d, offset = %ld\n\n", fd, offset);
        } else if (whence == SEEK_END) {
            var_off_t = old_lseek(fd, offset, whence);
            __android_log_print(ANDROID_LOG_DEBUG, "\n\ninlinehook=====00",
                                "lseek(SEEK_END): fd = %d, offset = %ld\n\n", fd, offset);
        } else if (whence == SEEK_SET) {
            var_off_t = old_lseek(fd, offset + SUNINFO, whence);
            __android_log_print(ANDROID_LOG_DEBUG, "\n\ninlinehook=====",
                                "lseek(SEEK_SET): fd = %d, offset = %ld\n\n", fd, offset + SUNINFO);
        }

        return var_off_t - SUNINFO;
    }

    return old_lseek(fd, offset, whence);
}

static off64_t __nativehook_impl_android_lseek64(int fd, off64_t offset, int whence) {
    int var_off64_t = 0;
    FILE_FD_INFO_S *pstFileFdInfo = findFileFdInfo(fd);

    if (NULL != pstFileFdInfo) {
        __android_log_print(ANDROID_LOG_DEBUG, "\ninlinehook=====",
                            "init , lseek64 000: fd = %d, offset = %d, whence = %d\n", fd, offset,
                            whence);


        if (whence == SEEK_CUR) {
            var_off64_t = old_lseek64(fd, offset, whence);
            __android_log_print(ANDROID_LOG_DEBUG, "\n\ninlinehook=====",
                                "lseek64(SEEK_CUR): fd = %d, offset = %lld\n\n", fd, offset);
        } else if (whence == SEEK_END) {
            var_off64_t = old_lseek64(fd, offset, whence);
            __android_log_print(ANDROID_LOG_DEBUG, "\n\ninlinehook=====",
                                "lseek64(SEEK_END): fd = %d, offset = %lld\n\n", fd, offset);
        } else if (whence == SEEK_SET) {
            var_off64_t = old_lseek64(fd, offset + SUNINFO, whence);
            __android_log_print(ANDROID_LOG_DEBUG, "\n\ninlinehook=====",
                                "lseek64(SEEK_SET): fd = %d, offset = %lld\n\n", fd,
                                offset + SUNINFO);
        }

        return var_off64_t - SUNINFO;
    }

    return old_lseek64(fd, offset, whence);
}

static int __nativehook_impl_android_fstat(int fd, struct stat *statbuf) {
    int result = 0;

    result = old_fstat(fd, statbuf);

    FILE_FD_INFO_S *pstFileFdInfo = findFileFdInfo(fd);
    if (NULL != pstFileFdInfo) {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook=====", "before, fstat: filepath = %s, fd = %d, fileSize = %lld",
                            pstFileFdInfo->szFilePath, pstFileFdInfo->fd, statbuf->st_size);

        if (statbuf->st_size != 0) {
            statbuf->st_size = statbuf->st_size - SUNINFO;
        }

        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook=====",
                            "after, fstat: fd = %d, fileSize = %lld", fd, statbuf->st_size);
    }

    return result;
}

void startInlineHook(void) {
    void *pOpenat = (void *) __openat;

    //lseek
    if (registerInlineHook((uint32_t) lseek, (uint32_t) __nativehook_impl_android_lseek,
                           (uint32_t **) &old_lseek) != ELE7EN_OK) { ;
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==lseek== start %p\n",
                            lseek);
        inlineHook((uint32_t) lseek);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==lseek== end\n");
    }

    //lseek64
    if (registerInlineHook((uint32_t) lseek64, (uint32_t) __nativehook_impl_android_lseek64,
                           (uint32_t **) &old_lseek64) != ELE7EN_OK) { ;
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook",
                            "inline hook ==lseek64== start %p\n",
                            lseek64);
        inlineHook((uint32_t) lseek64);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==lseek64== end\n");
    }


    //close
    if (registerInlineHook((uint32_t) close, (uint32_t) __nativehook_impl_android_close,
                           (uint32_t **) &old_close) != ELE7EN_OK) { ;
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==close== start %p\n",
                            close);
        inlineHook((uint32_t) close);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==close== end\n");
    }

    //pread
    if (registerInlineHook((uint32_t) pread, (uint32_t) __nativehook_impl_android_pread,
                           (uint32_t **) &old_pread) != ELE7EN_OK) { ;
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==pread== start %p\n",
                            pread);
        inlineHook((uint32_t) pread);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==pread== end\n");
    }

    //pwrite
    if (registerInlineHook((uint32_t) pwrite, (uint32_t) __nativehook_impl_android_pwrite,
                           (uint32_t **) &old_pwrite) != ELE7EN_OK) { ;
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook",
                            "inline hook ==pwrite== start %p\n",
                            pwrite);
        inlineHook((uint32_t) pwrite);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==pwrite== end\n");
    }

    //read
    if (registerInlineHook((uint32_t) read, (uint32_t) __nativehook_impl_android_read,
                           (uint32_t **) &old_read) != ELE7EN_OK) { ;
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==read== start %p\n",
                            read);
        inlineHook((uint32_t) read);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==read== end\n");
    }

    //write
    if (registerInlineHook((uint32_t) write, (uint32_t) __nativehook_impl_android_write,
                           (uint32_t **) &old_write) != ELE7EN_OK) { ;
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==write== start %p\n",
                            write);
        inlineHook((uint32_t) write);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==write== end\n");
    }


    //__openat
    if (registerInlineHook((uint32_t) pOpenat, (uint32_t) __nativehook_impl_android_openat,
                           (uint32_t **) &old_openat) != ELE7EN_OK) { ;
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook",
                            "inline hook ==__openat== start %p\n",
                            pOpenat);
        inlineHook((uint32_t) pOpenat);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==__openat== end\n");
    }


    //fstat
    if (registerInlineHook((uint32_t) fstat, (uint32_t) __nativehook_impl_android_fstat,
                           (uint32_t **) &old_fstat) != ELE7EN_OK) { ;
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==fstat== start %p\n",
                            fstat);
        inlineHook((uint32_t) fstat);
        __android_log_print(ANDROID_LOG_DEBUG, "inlinehook", "inline hook ==fstat== end\n");
    }
}
