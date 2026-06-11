pipeline {
    agent none
    options {
        timeout(time: 60, unit: 'MINUTES') 
    }
    stages {
        stage("Build and Test") {
            parallel {
                stage("Windows") {
                   agent { label 'couchbase-lite-net-validation' }
                   environment {
                       BRANCH = "${BRANCH_NAME}"
                   }
                   steps {
                       powershell 'jenkins\\jenkins_win.ps1'
                   }
                }
                stage("Apple") {
                    agent { label 'sonoma'  }
                    environment {
                        BRANCH = "${BRANCH_NAME}"
                        GH_PAT = credentials("cbl-bot-github-pat")
                        KEYCHAIN_PWD = credentials("mobile-mac-mini-keychain")
                    }
                    steps {
                        sh 'jenkins/jenkins_ios.sh'
                        sh 'couchbase-lite-c/scripts/coverage_macos.sh --push'
                    }
                }
                stage("Linux") {
                    agent { label 's61113u16 (litecore)' }
                    environment {
                       BRANCH = "${BRANCH_NAME}"
                       CC = "gcc-10"
                       CXX = "g++-10"
                    }
                    steps {
                        sh 'jenkins/jenkins_unix.sh'
                    }
                }
            }
        }
    }
}
