using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class Enemy : MonoBehaviour
{
    public float speed = 0.5f;
    public float hitPoints = 10;
    public float maxDamage = 10;

    public float smoothTime = 3.0f;


    private Vector3 smoothVelocity = Vector3.zero;
    private Vector3 playerPos;

    public Transform player;

	// Use this for initialization
	void Start ()
    {

	}
	
	// Update is called once per frame
	void Update ()
    {
        float distance = Vector3.Distance(transform.position, player.position);

        transform.position = Vector3.SmoothDamp(transform.position, player.position, ref smoothVelocity, smoothTime);
	}
}
