using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Enemy : MonoBehaviour
{
    public float speed = 0.5f;
    public float hitPoints = 10.0f;
    public float maxDamage = 10;

    public float smoothTime = 3.0f;


    private Vector3 smoothVelocity = Vector3.zero;
    private Vector3 playerPos;

    private GameObject player;
    private Transform playerTransform;

	// Use this for initialization
	void Start ()
    {
        player = GameObject.Find("Player");
        playerTransform = player.transform;
    }
	
	// Update is called once per frame
	void Update ()
    {
        float distance = Vector3.Distance(transform.position, playerTransform.position);

        transform.position = Vector3.SmoothDamp(transform.position, playerTransform.position, ref smoothVelocity, smoothTime);
	}
}
