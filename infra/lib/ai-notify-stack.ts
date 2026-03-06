import * as cdk from 'aws-cdk-lib';
import * as iam from 'aws-cdk-lib/aws-iam';
import * as iot from 'aws-cdk-lib/aws-iot';
import * as ssm from 'aws-cdk-lib/aws-ssm';
import { Construct } from 'constructs';
import { ThingWithCert } from 'cdk-iot-core-certificates-v3';

export class AiNotifyStack extends cdk.Stack {
    constructor(scope: Construct, id: string, props?: cdk.StackProps) {
        super(scope, id, props);

        const projectName = 'AiNotify';
        const thingName   = `${projectName}Thing`;     // AiNotifyThing
        const topic       = 'ai-notify/events';

        // IoT Thing + certificate.
        // ThingWithCert saves to SSM automatically:
        //   /AiNotify/AiNotifyThing/certPem
        //   /AiNotify/AiNotifyThing/privKey
        // certId resolves to the certificate ID (not the full ARN). If upgrading
        // cdk-iot-core-certificates-v3, verify certId still returns just the ID,
        // not a full ARN — the certArn construction below depends on this.
        const { certId } = new ThingWithCert(this, 'Thing', {
            thingName,
            saveToParamStore: true,
            paramPrefix: projectName,
        });

        // IoT policy: device may only connect, subscribe, and receive on the event topic.
        // The device never publishes — that is the Lambda's job.
        const iotPolicy = new iot.CfnPolicy(this, 'DevicePolicy', {
            policyName: `${projectName}DevicePolicy`,
            policyDocument: {
                Version: '2012-10-17',
                Statement: [
                    {
                        Effect:   'Allow',
                        Action:   'iot:Connect',
                        Resource: `arn:aws:iot:${this.region}:${this.account}:client/${thingName}`,
                    },
                    {
                        Effect:   'Allow',
                        Action:   'iot:Subscribe',
                        Resource: `arn:aws:iot:${this.region}:${this.account}:topicfilter/${topic}`,
                    },
                    {
                        Effect:   'Allow',
                        Action:   'iot:Receive',
                        Resource: `arn:aws:iot:${this.region}:${this.account}:topic/${topic}`,
                    },
                ],
            },
        });

        // Attach the device policy to the certificate.
        const certArn = `arn:aws:iot:${this.region}:${this.account}:cert/${certId}`;
        const policyAttachment = new iot.CfnPolicyPrincipalAttachment(this, 'PolicyAttachment', {
            policyName: `${projectName}DevicePolicy`,
            principal:  certArn,
        });
        policyAttachment.addDependency(iotPolicy);

        // IAM Managed Policy for the nakom.is Lambda to publish to the event topic
        // and read the IoT endpoint from SSM.
        const publishPolicy = new iam.ManagedPolicy(this, 'LambdaPublishPolicy', {
            managedPolicyName: 'AiNotifyLambdaPublishPolicy',
            description: 'Allows publishing MQTT events to ai-notify/events and reading the IoT endpoint from SSM',
            statements: [
                new iam.PolicyStatement({
                    effect:    iam.Effect.ALLOW,
                    actions:   ['iot:Publish'],
                    resources: [`arn:aws:iot:${this.region}:${this.account}:topic/${topic}`],
                }),
                // /AiNotify/IotEndpoint must be created manually after first deploy.
                // See CLAUDE.md for the aws ssm put-parameter command.
                // Without this parameter, the Lambda cold start will fail.
                new iam.PolicyStatement({
                    effect:    iam.Effect.ALLOW,
                    actions:   ['ssm:GetParameter'],
                    resources: [
                        `arn:aws:ssm:${this.region}:${this.account}:parameter/AiNotify/IotEndpoint`,
                    ],
                }),
            ],
        });

        // Store the policy ARN in SSM so the nakom.is CDK stack can reference it
        // without hardcoding the ARN.
        // Default removal policy is RETAIN — if stack is destroyed and redeployed,
        // this parameter must be manually deleted first to avoid a conflict.
        new ssm.StringParameter(this, 'IotPublishPolicyArn', {
            parameterName: `/${projectName}/IotPublishPolicyArn`,
            stringValue:   publishPolicy.managedPolicyArn,
            description:   'ARN of AiNotifyLambdaPublishPolicy — attach to nakom.is stream Lambda',
        });

        new cdk.CfnOutput(this, 'ThingName',         { value: thingName });
        new cdk.CfnOutput(this, 'PublishPolicyArn',  {
            value:       publishPolicy.managedPolicyArn,
            description: 'Attach this to the nakomis-chat-stream Lambda execution role',
        });
        new cdk.CfnOutput(this, 'SSMCertPath',    { value: `/${projectName}/${thingName}/certPem` });
        new cdk.CfnOutput(this, 'SSMPrivKeyPath', { value: `/${projectName}/${thingName}/privKey` });
    }
}
