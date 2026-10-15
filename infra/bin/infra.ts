#!/usr/bin/env node
import * as cdk from 'aws-cdk-lib';
import { AiNotifyStack } from '../lib/ai-notify-stack';

const app = new cdk.App();
new AiNotifyStack(app, 'AiNotifyStack', {
    env: {
        account: process.env.CDK_DEFAULT_ACCOUNT,
        region:  process.env.CDK_DEFAULT_REGION,
    },
});
